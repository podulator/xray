#!/bin/bash

gcc ./xray.c -o xray.exe -lmnl -lnfnetlink -lnetfilter_queue  && chmod +x ./xray.exe && echo "built..."
