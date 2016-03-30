#!/bin/bash
curl --silent -X get $B/ReadEntityAdmin/AuthTable | ./authfields.py
