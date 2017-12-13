# xray

experiments around trying to get an xray daemon happening in userland based on libnetfilter_queue

## dependencies
apt-get install libmnl-dev libnetfilter-queue-dev libnfnetlink-dev nfqueue-bindings-perl

iptables rule 
iptables -t filter -A OUTPUT -p tcp -j NFQUEUE --queue-num $outboundQueue --queue-bypass
needs to be codified from
https://doxygen.openinfosecfoundation.org/source-nfq_8c_source.html#l00514

## bookmarks
  * http://www.roman10.net/2011/12/01/how-to-configure-install-and-use-libnefilter_queue-on-linux/
  * https://code.tutsplus.com/tutorials/http-headers-for-dummies--net-8039
  * http://wiki.tldp.org/iptc%20library%20HOWTO
  * http://tldp.org/HOWTO/Querying-libiptc-HOWTO/index.html
  * https://bani.com.br/2012/05/programmatically-managing-iptables-rules-in-c-iptc/
  * https://www.netfilter.org/documentation/HOWTO/netfilter-hacking-HOWTO.txt
  * https://doxygen.openinfosecfoundation.org/source-nfq_8c_source.html#l00514
  * http://netfilter.org/projects/libnetfilter_queue/doxygen/
  * https://github.com/chifflier/nfqueue-bindings/blob/master/perl/libnetfilter_queue_perl.i

## question asked here 
  * https://marc.info/?l=netfilter&r=1&b=201712&w=2&

 
