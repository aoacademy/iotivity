# Introduction
This is a IoTivity APIs implementation application using the "random pin" security model
There are 2 applications; server and client, which need to be running from 2
different terminals regardless whether those 2 terminals are running within
the same machine or not as long as they can discover each other.
These 2 applications are verified on
* Regular Ubuntu machine
* Raspbian running on Raspberry Pi 3 and Raspberry Pi Zero W

# Install IoTivity

## step 1 : install all essential things to compile the code on a Linux machine

sudo apt-get -y install build-essential git libtool \
autoconf valgrind doxygen wget unzip cmake libboost-dev \
libboost-program-options-dev libboost-thread-dev uuid-dev \
libexpat1-dev libglib2.0-dev libsqlite3-dev libcurl4-gnutls-dev

additional necessary tools:

git clone https://github.com/01org/tinycbor.git \
extlibs/tinycbor/tinycbor -b v0.4.1

git clone https://github.com/ARMmbed/mbedtls.git \
extlibs/mbedtls/mbedtls -b mbedtls-2.4.2


## step 2 : clone repo

git clone https://gerrit.iotivity.org/gerrit/iotivity

Up to this point all steps would be done on both devices. Additionally Mraa library will be installed on raspberry pi device to control LED:

git clone https://github.com/intel-iot-devkit/mraa.git
mkdir mraa/build && cd mraa/build
cmake .. && make && sudo make install

# Building the applications

To build the applications, first copy paste the files within ThingsManager folder into /iotivity/examples/OCFSecure

Then type the following scons command from the root
directory of IoTivity

```
$ scons examples/OCFSecure TARGET_TRANSPORT=IP
```

To speed up the build procerss and utilize more than a single core, you can
add the -j option followed by the number of cores to utilize for building
the applications

To build the applications in the debug mode, you can add the following option
to the scons command RELEASE=0

Run this command on both devices.

# Running the applications

To run the applications on a regular machine with Ubuntu, change the directory
to `out/linux/x86_64/release/examples/OCFSecure` with the following command
```
$ cd out/linux/x86_64/release/examples/OCFSecure/
```
Next, run the server application with the following command
```
$ ./server
```
open up another terminal window within the same directory
(shortcut:Ctrl+Shift+n) and run the client application with the following
command.
```
$ ./client
```
Since there is no physical led attached to a regular machine with Ubuntu, the
led resource will be simulated. Otherwise, if you are running these
applications on a Raspberry Pi or Intel Joule board then you would need to
run the server application with the sudo command so the server application
can have access to the hardware of the board.

To run the application on a raspberry pi device, change to the output directory where the sample application executable files were
created. (Note the directory name armv7l ends with the lower-case letter “l” not a digit
one.) Because the application directly accesses hardware (as did the blink-io mraa test),
it needs to run as root (administrator) with the “sudo” command. Note too, you’re going
to run the server as a background app so we can run the client app in the foreground in
the next step:

cd ~/iot/iotivity/out/linux/armv7l/release/examples/OCFSecure
sudo ./server &

If you get an error saying “./server: error while loading shared libraries:
libmraa.so.1: cannot open shared object file: No such file or directory”, use
this command to fix the error and try running the server application again:

sudo env LD_LIBRARY_PATH=~/iot/mraa/build/src && sudo ldconfig

If successful, you should be able to see the Initial Menu.

To run the applications on Raspberry Pi with Raspian then make sure to connect
an LED on hardware pin 4 which is gpio 7.

Have a jumper wire from GPIO 7 to the positive terminal of the led.
Then connect a resistor from the negative terminal of the led to ground.

Also, it is important to remember to run the server application in sudo mode as
hardware access is a super user privilege. Otherwise, you might get an error of
unknown gpio or the app might run but will not control the led and you might
think you connected the led on the wrong pin which may not be the case.

# Using client application

First thing to do is to provision the device. To do this, select 'provision' (by entering menu number) and then
select 'discover unowned devices'. Then you can select 'Register unowned devices'. A success
message will be shown and you can make sure of it by selecting 'Discover owned devices'. You should
see that the device now is moved to this list. Now the device is ready to use.

To use it, we first need to discover the resources which is done by entering number 2. then we will be able to
send the requests and see the response. 

# Resetting the server app
Sometimes it is necessary to reset the server application. For example, the
server app needs to be reset during OCF conformance testing or to be
onboarded by an OnBoarding tool.

In order to reset the server app, make sure it is not running. If it is running,
then kill it with Ctrl+C.
Copy the `ocf_svr_db_server_RFOTM.dat` from the project directory to the project
output directory and name
From the output directory from which the server application can be executed,
type.

# Known issues

1. Sometimes, the applications will not run because of not finding some library.
In this case, you would need to export the `LD_LIBRARY_PATH` to the environment.
    ```
    export LD_LIBRARY_PATH=<output dir orwherever the library is>
    ```
    Also, since you need to run the server application in privileged mode, you
would need to type this command
    ```
    $sudo ldconfig
    ```
and you might also need to type
    ```
    $sudo env LD_LIBRARY_PATH=<output dir or wherever the library is>
    ```


# Example Directory

There are 8 files in the example directory.
* `client.cpp`
    * This is the client program
* `oic_svr_db_client.dat`
    * This is the cbor format of the secure virtual resource database and it is used by
the client application
* `ocf_svr_db_server_RFOTM.dat`
    * This is the cbor format of the secure virtual resource database
* `oic_prvn_mng.db`
    * This is the database file used for provisioning and onboarding actions
* `README.md`
    * This is this file :)
* `SConscript`
    * This is the script that is being used by the scons tool to know how
to build the sample applications and what needs to be copied to the output
directory.
* `server.cpp`
    * This is the server program.
* `utilities.c`
    * this is a supplementary program containing custom utility c functions
that help with reporting log messages mainly as of current.
