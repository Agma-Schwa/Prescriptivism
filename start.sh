#!/usr/bin/env bash

cmake --build out
./PrescriptivismServer --pwd password &
srv=$!

sleep .2
./Prescriptivism --connect localhost --name testuser1 --password password &
client1=$!
./Prescriptivism --connect localhost --name testuser2 --password password &
client2=$!

read -p "Press enter to kill server and clients"
kill $srv
kill $client1
kill $client2
