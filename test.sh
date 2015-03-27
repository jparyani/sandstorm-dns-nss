#!/bin/bash

http-server -p 10000 &
sleep 2
#bin/sandstorm-ip-bridge &
#sleep 1
curl google.com || exit 1
echo "success"
sleep 5
exit 0
