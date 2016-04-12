#!/bin/bash
PWD=\"Password\"\:\"$2\"
PA=\"DataPartition\"\:\"$3\"
PR=\"DataRow\"\:\"$4\"
curl -i -X put -H"$H" -d "{$PWD, $PA, $PR}" $D/UpdateEntityAdmin/AuthTable/Userid/$1
