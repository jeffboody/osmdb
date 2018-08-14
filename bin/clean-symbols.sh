# exchange non-ascii characters
sed -e "s/´/'/" -e "s/’/'/" -e 's/–/-/' -e 's/—/-/' $1 > $2
