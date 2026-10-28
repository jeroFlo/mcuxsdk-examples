# wifi_provisioning

## Overview

The wifi_provisioning demo application configures Wi-Fi credentials and connects the board to a wireless network.

Before building the example application select Wi-Fi module macro in the app_config.h. 
(see `#define WIFI_<SoC Name>_BOARD_<Module Name>`).
## Replacement Python CLI Workflow

Connect your computer to the `RW612-Provision` access point. The board prints its
provisioning IP address on the serial terminal. Then run:

```bash
python3 provisioning_app/provision.py
```

The command discovers `rw612-provision.local` using mDNS, then sends SSID
`SmartBulb1` and password `123456789` to TCP port 15000. You can provide different
credentials directly:

```bash
python3 provisioning_app/provision.py --ssid MyNetwork --password MyPassword
```

If mDNS is unavailable, use the IP printed by the board:

```bash
python3 provisioning_app/provision.py --device-ip 192.168.10.1
```
For more information about Wi-Fi module connection see:
- readme_modules.txt

- [Getting started guide](https://www.nxp.com/document/guide/getting-started-with-nxp-wi-fi-modules-using-i-mx-rt-platform:GS-WIFI-MODULES-IMXRT-PLATFORM)



## Prepare the Demo
1.  Connect a micro USB cable between the PC host and the CMSIS DAP USB port on the board
2.  Open a serial terminal with the following settings:
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control
3.  Connect the Wi-Fi module. Refer to readme_modules.txt and the [Supported boards](#supported-boards) section.
4.  Prior building of example you may want to set following symbols:
	- WIFI_SSID and WIFI_PASSWORD according settings of your AP.
	- EXAMPLE_MQTT_SERVER_HOST and EXAMPLE_MQTT_SERVER_PORT to aim to your broker.
5.  Download the program to the target board.
6.  Either press the reset button on your board or launch the debugger in your IDE to begin running the demo.


## Running the demo
When the demo runs, the log would be seen on the terminal like:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
************************************************
MQTT client example
************************************************
[i] Initializing Wi-Fi connection... 
MAC Address: 48:E7:DA:9A:CE:39 
-------- wlan_event_callback 11 --------
[i] Successfully initialized Wi-Fi module
Connecting as client to ssid: my_network with password my_password
-------- wlan_event_callback 0 --------
[i] Connected to Wi-Fi
ssid: my_network
[!]passphrase: my_password

IPv4 Address     : 192.168.0.249
IPv4 Subnet mask : 255.255.255.0
IPv4 Gateway     : 0.0.0.0

Resolving "test.mosquitto.org"...
Connecting to MQTT broker at 5.196.78.28...
MQTT client "nxp_322c480e82967d6a" connected.
Subscribing to the topic "lwip_topic/#" with QoS 0...
Subscribing to the topic "lwip_other/#" with QoS 1...
Subscribed to the topic "lwip_topic/#".
Subscribed to the topic "lwip_other/#".
Going to publish to the topic "lwip_topic/100"...
Received 18 bytes from the topic "lwip_topic/100": "message from board"
Published to the topic "lwip_topic/100".
Going to publish to the topic "lwip_topic/100"...
Received 18 bytes from the topic "lwip_topic/100": "message from board"
Published to the topic "lwip_topic/100".
Going to publish to the topic "lwip_topic/100"...
Received 18 bytes from the topic "lwip_topic/100": "message from board"
Published to the topic "lwip_topic/100".
Going to publish to the topic "lwip_topic/100"...
Received 18 bytes from the topic "lwip_topic/100": "message from board"
Published to the topic "lwip_topic/100".
Going to publish to the topic "lwip_topic/100"...
Received 18 bytes from the topic "lwip_topic/100": "message from board"
Published to the topic "lwip_topic/100".
Disconnected from MQTT broker.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Supported Boards
- [EVK-MIMXRT1064](../../_boards/evkmimxrt1064/wifi_examples/common/wifi_examples_readme.md)
- [FRDM-RW612](../../_boards/frdmrw612/wifi_examples/common/wifi_examples_readme.md)
- [RD-RW612-BGA](../../_boards/rdrw612bga/wifi_examples/common/wifi_examples_readme.md)
