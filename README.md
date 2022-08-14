# icmp_watch
Send batch requests for ICMP and show results in a console window to monitor availability of hosts.

![console](console.png "console")

## Limitations:

 * Uses ICMP sockets, which means that sysctl net.ipv4.ping_group_range needs to be set properly. (My Ubuntu 22.04 was configured correctly for this, by default.)
 * For now, IPv4 only, but adding IPv6 support shouldn't be hard.
 * Update frequency is currently hard-coded to 1 second.

## Features:

 * Super lightweight: uses 0.0% CPU/MEM on my machine.
 * Single file C source.
 * MIT license.

## Troubleshoot

If the tool fails on permissions when creating the socket, please do:

`$ sysctl -w net.ipv4.ping_group_range="0 2147483647"` to allow users to create ICMP sockets. You can use "0 0" range if you want this for root only.

## Author

The icmp_watch tool is made by Abraham Stolk.

