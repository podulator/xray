#!/bin/bash

# iptables needs to be codified from
# https://doxygen.openinfosecfoundation.org/source-nfq_8c_source.html#l00514
# https://bani.com.br/2012/05/programmatically-managing-iptables-rules-in-c-iptc/
# https://www.netfilter.org/documentation/HOWTO/netfilter-hacking-HOWTO.txt
# https://doxygen.openinfosecfoundation.org/source-nfq_8c_source.html#l00514
# http://netfilter.org/projects/libnetfilter_queue/doxygen/
# http://www.netfilter.org/projects/libnetfilter_queue/doxygen/group__LibrarySetup.html

# question asked here 
# https://marc.info/?l=netfilter&r=1&b=201712&w=2&


trap ctrl_c INT
inboundQueue=1
outboundQueue=2
failOpen=" --fail-open"
failOpen=""

function delete_inbound_queue() {
	echo "deleting inbound queue :: iptables -t filter -D INPUT -p tcp -j NFQUEUE --queue-num $inboundQueue --queue-bypass$failOpen"
	iptables -t filter -D INPUT -p tcp -j NFQUEUE --queue-num $inboundQueue --queue-bypass$failOpen
}

function delete_outbound_queue() {
	echo "deleting outbound queue :: iptables -t filter -D OUTPUT -p tcp -j NFQUEUE --queue-num $outboundQueue --queue-bypass$failOpen"
	iptables -t filter -D OUTPUT -p tcp -j NFQUEUE --queue-num $outboundQueue --queue-bypass$failOpen
}
function ctrl_c() {
	echo ""
	echo "** Trapped CTRL-C **"
	echo "cleaning up"
	delete_inbound_queue
	delete_outbound_queue
	echo "done"
	exit 0
}

echo "setting up iptables"
if [[ -f /proc/net/netfilter/nfnetlink_queue ]]; then
	echo "/proc/net/netfilter/nfnetlink_queue exists, so we have pre-existing queues to deal with"
	position=0
	while IFS=' ' read -r line || [[ -n "$line" ]]; do
		echo "Text read from file: $line"
		position="$( cut -d ' ' -f 1 <<< "$line" )"
		if [[ $position -gt $inboundQueue ]]; then
			# just go one higher than the max
			inboundQueue=$((position + 1))
		fi
	done < "/proc/net/netfilter/nfnetlink_queue"
	# and make outbound == inbound + 1
	outboundQueue=$((inboundQueue + 1))
else
	echo "no pre-existing queues to worry about, going with default numbers"
fi

echo "inboundQueue = $inboundQueue"
echo "outboundQueue = $outboundQueue"

echo "creating inbound queue as :: iptables -t filter -A INPUT -p tcp -j NFQUEUE --queue-num $inboundQueue --queue-bypass$failOpen"
iptables -t filter -A INPUT -p tcp -j NFQUEUE --queue-num $inboundQueue --queue-bypass$failOpen
if [[ $? -ne 0 ]]; then
	exit 1
else
	echo "inbound queue created"
fi

echo "creating outbound queue as :: iptables -t filter -A OUTPUT -p tcp -j NFQUEUE --queue-num $outboundQueue --queue-bypass$failOpen"
iptables -t filter -A OUTPUT -p tcp -j NFQUEUE --queue-num $outboundQueue --queue-bypass$failOpen
if [[ $? -ne 0 ]]; then
	delete_inbound_queue
	exit 1
else
	echo "outbound queue created"
fi

echo "running the app as :: ./xray.exe -i $inboundQueue -o $outboundQueue"
echo 'Hit CTRL+C to quit'

./xray.exe -i $inboundQueue -o $outboundQueue

if [[ $? -ne 0 ]]; then
	delete_inbound_queue
	delete_outbound_queue
fi

#while :; do sleep 1; done
