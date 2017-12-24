/************************************************************************

    mainwindow.cpp

    QT GUI Capture application for Domesday Duplicator
    DomesdayDuplicator - LaserDisc RF sampler
    Copyright (C) 2017 Simon Inns

    This file is part of Domesday Duplicator.

    Domesday Duplicator is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Email: simon.inns@gmail.com

************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Set up the USB device object
    domDupUsbDevice = new usbDevice;

    // Set up the LVDP control communication
    playerControl = new lvdpControl;

    // Set the capture flag to false (not capturing)
    captureFlag = false;

    // Set up the transfer button
    ui->transferPushButton->setText(tr("Start capturing"));

    // Add some default status text to show the state of the USB device
    status = new QLabel;
    ui->statusBar->addWidget(status);
    if (domDupUsbDevice->isConnected()) {
        status->setText(tr("Connected"));
        ui->transferPushButton->setEnabled(true);
        ui->testModeCheckBox->setEnabled(true);
    } else {
        status->setText(tr("Domesday Duplicator USB device not connected"));
        ui->transferPushButton->setEnabled(false);
        ui->testModeCheckBox->setEnabled(false);
    }

    // Connect to the usb device's signals to show insertion/removal events
    connect(domDupUsbDevice, SIGNAL(statusChanged(bool)), SLOT(usbStatusChanged(bool)));

    // Set up the text labels
    ui->capturedDataLabel->setText(tr("0"));
    ui->transferSpeedLabel->setText(tr("0"));
    ui->diskBufferProgressBar->setValue(0);
    ui->testModeFailLabel->setText(tr("0"));

    // Set up a timer for updating capture results
    captureTimer = new QTimer(this);
    connect(captureTimer, SIGNAL(timeout()), this, SLOT(updateCaptureInfo()));

    // Set up a timer for updated the player control information
    updateTimer = new QTimer(this);
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(updatePlayerControlInfo()));

    // Disable the start transfer button (until a destination file name is supplied)
    ui->transferPushButton->setEnabled(false);

    // Disable the test mode checkbox until a USB device is connected
    ui->testModeCheckBox->setEnabled(false);

    // Create the about dialogue
    aboutDomDup = new aboutDialog(this);

    // Create the LVDP serial settings dialogue
    lvdpSerialPortSelect = new serialPortSelectDialog(this);

    // Create the LVDP control dialogue
    lvdpPlayerControl = new playerControlDialog(this);

    // Connect to the serial port configured signal
    connect(lvdpSerialPortSelect, SIGNAL(serialPortChanged()), this, SLOT(serialPortStatusChange()));

    // Start updating the player control information
    updateTimer->start(100); // Update 10 times a second (1000 / 10 = 100)

    // Connect PIC control events to the handler
    connect(lvdpPlayerControl, SIGNAL(playerControlEvent(playerControlDialog::PlayerControlEvents, quint32, quint32)), this,
            SLOT(handlePlayerControlEvent(playerControlDialog::PlayerControlEvents, quint32, quint32)));
}

MainWindow::~MainWindow()
{
    // Stop the player control
    playerControl->stopStateMachine();

    // Delete the UI
    delete ui;
}

// Update the USB device status based on signals from the USB detection
void MainWindow::usbStatusChanged(bool usbStatus)
{
    if (usbStatus) {
        qDebug() << "MainWindow::usbStatusChanged(): USB device is connected";
        status->setText(tr("Connected"));

        // Enable transfer if there is a filename selected
        if (!fileName.isEmpty()) {
            ui->transferPushButton->setEnabled(true);
            ui->testModeCheckBox->setEnabled(true);
            ui->testModeCheckBox->setChecked(false);
        }
    } else {
        qDebug() << "MainWindow::usbStatusChanged(): USB device is not connected";
        status->setText(tr("Domesday Duplicator USB device not connected"));

        // Are we mid-capture?
        if (captureFlag) {
            qDebug() << "MainWindow::usbStatusChanged(): USB device removed during capture.  Attempting to clean up";
            this->stopTransfer();
        }

        ui->transferPushButton->setEnabled(false);
        ui->testModeCheckBox->setEnabled(false);
    }
}

// Menu option "About" triggered
void MainWindow::on_actionAbout_triggered()
{
    // Show about about dialogue
    aboutDomDup->show();
}

// Menu option "Save As" triggered
void MainWindow::on_actionSave_As_triggered()
{
    if (fileName.isEmpty()) {
        // No previous file name selected.  Fill in the default location and file name
        fileName = QFileDialog::getSaveFileName(this, tr("Save RF capture as"), QDir::homePath()+tr("/rfcapture.raw"), tr("RAW Files (*.raw)"));
    } else {
        // Previous file name selected, fill it in again
        fileName = QFileDialog::getSaveFileName(this, tr("Save RF capture as"), fileName, tr("RAW Files (*.raw)"));
    }

    if (fileName.isEmpty()) {
        // No file name was specified
        qDebug() << "MainWindow::on_actionSave_As_triggered(): User did not supply a file name";
        ui->transferPushButton->setEnabled(false);
        ui->testModeCheckBox->setEnabled(false);
    } else {
        // File name specified
        qDebug() << "MainWindow::on_actionSave_As_triggered(): Save as filename = " << fileName;

        // Enable the capture control buttons (if a USB device is connected)
        if (domDupUsbDevice->isConnected()) {
            ui->transferPushButton->setEnabled(true);
            ui->testModeCheckBox->setEnabled(true);
        }
    }
}

// Menu option "Quit" triggered
void MainWindow::on_actionQuit_triggered()
{
    // Quit the application
    qApp->quit();
}

// Transfer push button clicked
void MainWindow::on_transferPushButton_clicked()
{
    // Check if we are currently capturing
    if (captureFlag) {
        // Stop capturing
        qDebug() << "MainWindow::on_transferPushButton_clicked(): Stopping capture";
        stopTransfer();
    } else {
        // Start capturing
        qDebug() << "MainWindow::on_transferPushButton_clicked(): Starting capture";
        startTransfer();
    }
}

// Start USB capture transfer
void MainWindow::startTransfer(void)
{
    bool responseFlag = false;

    // Ensure we have a file name
    if (fileName.isEmpty()) {
        qDebug() << "MainWindow::startTransfer(): No file name specified, cannot start transfer";
        return;
    }

    if (captureFlag == false) {
        qDebug() << "MainWindow::startTransfer(): Starting transfer";

        // Verify that the USB device is still connected
        if (domDupUsbDevice->isConnected()) {
            // Set the capture flag
            captureFlag = true;

            // Update the transfer button text
            ui->transferPushButton->setText(tr("Stop capturing"));

            // Disable the test mode check box
            ui->testModeCheckBox->setEnabled(false);

            // Configure and open the USB device
            responseFlag = domDupUsbDevice->openDevice();

            if (responseFlag) {
                qDebug() << "MainWindow::startTransfer(): USB device opened";

                // Send start transfer vendor specific USB command
                domDupUsbDevice->sendVendorSpecificCommand(0xB5, 1);

                // Start the transfer (pass test mode on/off state)
                domDupUsbDevice->startBulkRead(ui->testModeCheckBox->isChecked(), fileName);

                // Start a timer to display the transfer information
                captureTimer->start(100); // Update 10 times a second (1000 / 10 = 100)
            } else {
                // Could not open USB device
                qDebug() << "MainWindow::startTransfer(): Cannot start transfer - Opening USB device failed";
                captureFlag = false;
                ui->transferPushButton->setText(tr("Start capturing"));
            }
        } else {
            // Cannot start transfer; USB device not detected
            qDebug() << "MainWindow::startTransfer(): Cannot start transfer - USB device not connected";
        }
    } else {
        qDebug() << "MainWindow::startTransfer(): Called, but transfer is already in progress";
    }
}

// Stop USB capture transfer
void MainWindow::stopTransfer(void)
{
    if (captureFlag == true) {
        qDebug() << "MainWindow::stopTransfer(): Stopping transfer";

        // Disable the button whilst we stop the transfer
        ui->transferPushButton->setEnabled(false);

        // Set the capture flag
        captureFlag = false;

        // Stop the transfer
        domDupUsbDevice->stopBulkRead();

        // Stop the timer displaying the transfer information
        captureTimer->stop();

        // Send stop transfer vendor specific USB command
        domDupUsbDevice->sendVendorSpecificCommand(0xB5, 0);

        // Close the USB device
        domDupUsbDevice->closeDevice();

        // Update the transfer button text
        ui->transferPushButton->setText(tr("Start capturing"));

        // Enable the test mode check box
        ui->testModeCheckBox->setEnabled(true);

        // Enable the transfer button
        ui->transferPushButton->setEnabled(true);
    } else {
        qDebug() << "MainWindow::stopTransfer(): Called, but transfer is not in progress";
    }
}

// Test mode check box toggled
void MainWindow::on_testModeCheckBox_toggled(bool checked)
{
    bool responseFlag = false;

    if (checked) {
        // Test mode on
        qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Test mode data generation on";

        // Verify that the USB device is still connected
        if (domDupUsbDevice->isConnected()) {
            // Configure and open the USB device
            domDupUsbDevice->setupDevice();
            responseFlag = domDupUsbDevice->openDevice();

            if (responseFlag) {
                // Send test mode on vendor specific USB command
                domDupUsbDevice->sendVendorSpecificCommand(0xB6, 1);

                // Close the USB device
                domDupUsbDevice->closeDevice();
            } else {
                // Could not open device
                qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Cannot set test mode on - could not open USB device";
            }
        } else {
            // Device no longer connected
            qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Cannot set test mode on - USB device not connected";
        }
    } else {
        // Test mode off
        qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Test mode data generation off";

        // Verify that the USB device is still connected
        if (domDupUsbDevice->isConnected()) {
            // Configure and open the USB device
            domDupUsbDevice->setupDevice();
            responseFlag = domDupUsbDevice->openDevice();

            if (responseFlag) {
                // Send test mode off vendor specific USB command
                domDupUsbDevice->sendVendorSpecificCommand(0xB6, 0);

                // Close the USB device
                domDupUsbDevice->closeDevice();
            } else {
                // Could not open device
                qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Cannot set test mode off - could not open USB device";
            }
        } else {
            // Device no longer connected
            qDebug() << "MainWindow::on_testModeCheckBox_toggled(): Cannot set test mode off - USB device not connected";
        }
    }
}

// Update the capture information in the main window
void MainWindow::updateCaptureInfo(void)
{
    // Calculate and display the current amount of captured data (in MBytes)
    qreal capturedData = (qreal)(domDupUsbDevice->getPacketCounter() * domDupUsbDevice->getPacketSize()) / 1024;

    if (capturedData < 1024) ui->capturedDataLabel->setText(QString::number(capturedData, 'f', 0) + tr(" MBytes"));
    else {
        capturedData = capturedData / 1024;
        ui->capturedDataLabel->setText(QString::number(capturedData, 'f', 2) + tr(" GBytes"));
    }

    // Display the current transfer performance
    qreal transferPerformance = domDupUsbDevice->getTransferPerformance() / 1024;
    ui->transferSpeedLabel->setText(QString::number(transferPerformance, 'f', 0) + tr(" MBytes/sec"));

    // Display the available numbplayerControler of disk buffers (as a percentage)
    quint32 bufferAvailablity = (100 / domDupUsbDevice->getNumberOfDiskBuffers()) * domDupUsbDevice->getAvailableDiskBuffers();
    ui->diskBufferProgressBar->setValue(bufferAvailablity);

    // Display the number of test mode failures
    ui->testModeFailLabel->setText(QString::number(domDupUsbDevice->getTestFailureCounter()));
}

// Menu->PIC->Select player COM port triggered
void MainWindow::on_actionSelect_player_COM_port_triggered()
{
    lvdpSerialPortSelect->show();
}

// Menu->PIC->Show player control dialogue triggered
void MainWindow::on_actionShow_player_control_triggered()
{
    lvdpPlayerControl->show();
}

// Called when the serial port selection dialogue signals that the serial configuration has changed
void MainWindow::serialPortStatusChange(void)
{
    qDebug() << "MainWindow::serialPortStatusChange(): Serial port configuration changed";

    if(lvdpSerialPortSelect->isConfigured()) {
        // Connect to the player
        playerControl->serialConfigured(lvdpSerialPortSelect->getPortName(), lvdpSerialPortSelect->getBaudRate());
    } else {
        // Ensure the player is disconnected
        playerControl->serialUnconfigured();
    }
}

// Called by the player control information update timer
void MainWindow::updatePlayerControlInfo(void)
{
    //qDebug() << "MainWindow::updatePlayerControlInfo(): Updating";
    lvdpPlayerControl->updatePlayerControlInfo(
                playerControl->isConnected(),
                playerControl->isCav(),
                playerControl->currentFrameNumber(),
                playerControl->currentTimeCode(),
                playerControl->isPlaying()
                );
}

// Called by a player control event (from the PIC controls)
void MainWindow::handlePlayerControlEvent(playerControlDialog::PlayerControlEvents controlEvent,
                                          quint32 start, quint32 end)
{
    // Determine the event and process
    switch(controlEvent) {
        case playerControlDialog::PlayerControlEvents::event_playClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_play, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_pauseClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_pause, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_stopClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_stop, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_stepForwardsClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_stepForwards, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_stepBackwardsClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_stepBackwards, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_scanForwardsClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_scanForwards, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_scanBackwardsClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_scanBackwards, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_keyLockOnClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_keyLockOn, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_keyLockOffClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_keyLockOff, 0, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_gotoClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_goto, start, 0);
        break;

        case playerControlDialog::PlayerControlEvents::event_captureToClicked:
        playerControl->command(lvdpControl::PlayerCommands::command_captureTo, start, end);
        break;

        default:
            qDebug() << "MainWindow::handlePlayerControlEvent(): Unknown event received!";
    }
}
