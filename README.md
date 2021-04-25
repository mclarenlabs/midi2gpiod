# Introduction

MIDI is a protocol that was invented to send Note-On/Off and
Controller values between synthesizers connected by a serial-port
interface.  Because of its generality in the numbering of notes and
controllers, MIDI has found many other uses besides synthesizers.

This program `midi2gpiod` was designed for a RaspberryPi.  It watches
a specified MIDI port and converts Note-On/Off messages to GPIO on/off
commands using the `libgpiod` library.  By default, the program
watches for MIDI messages arriving from McLaren Labs' `rtpmidi`
program.  (See [https://mclarenlabs.com/rtpmidi](https://mclarenlabs.com/rtpmidi).
This allows GPIO pins to be "played" by sending MIDI commands over a network.

But you could also "play" GPIO pins directly by attaching a
MIDI keyboard directly to the USB port of the Raspberry Pi.

This program will probably also work on other devices that support the
`libgpiod` library, but it has not been tested on them.


## Requirements

To compile the program, the following must be installed.

- $ sudo apt-get install libasound2-dev
- $ sudo apt-get install libgpiod-dev


## Usage

``` console
$ midi2gpiod
```

Watch for MIDI notes emanating from port `rtpmidi:0` and turn GPIO
pins 25,26,27 on and off when middle-C, D and E are played on the
keyboard.

``` console
$ midi2gpiod -v -p midikbd:0
```

Watch for MIDI notes from a device named `midikbd` and convert to GPIO
on/off commands.  Log relevant MIDI messages received to stdout.


## Run MIDI2GPIOD as a Service

This program can be configured to start whenever the Pi reboots by making it a service.  The included file `midi2gpiod.service` is suitable for use with systemd.

To use: first copy `midi2gpiod.service` to `/etc/systemd/system` as user ROOT.  Then, type these commands to register the service with systemd.

``` console
$ sudo systemctl daemon-reload
```

After this you will be able to start the service with

``` console
$ sudo systemctl start midi2gpiod.service
```

To make the service survive a reboot and automatically restart, use the "enable" command.

``` console
$ sudo systemctl enable midi2gpiod.service
```

You can see log messages in Syslog.  Have a look here.

``` console
$ tail -f /var/log/syslog
```


## Designed to work with IOTRELAY

Have you ever wanted to control 120 Volt AC lights or appliances via MIDI?  This program combined with the IOTRELAY is the easiest way to do that.

See [https://iotrelay.com](https://iotrelay.com).

An IOTRELAY unit is controlled by a single GPIO pin.  It has four AC outlets:

- one is always ON
- two are normally OFF, but is turned ON by the GPIO pin going HIGH
- one is normally ON, but is turned OFF by the GPIO pin going HIGH

