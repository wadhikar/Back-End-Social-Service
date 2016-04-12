#!/bin/bash
curl --silent -X get $D/ReadEntityAdmin/AuthTable | ./authfields.py
