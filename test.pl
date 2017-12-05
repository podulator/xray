#!/usr/bin/perl -w
#
# see http://search.cpan.org/~atrak/NetPacket-0.04/

use strict;

BEGIN {
	push @INC,"perl";
	push @INC,"build/perl";
	push @INC,"NetPacket-0.04";
};

use nfqueue;

use NetPacket::IP qw(IP_PROTO_TCP);
use NetPacket::TCP;
use Socket qw(AF_INET AF_INET6);

my $debug = 1;
my $q;

sub cleanup()
{
	print "unbind\n";
	$q->unbind(AF_INET);
	print "close\n";
	$q->close();
}

my @http_checks = (
	"^Vary:"
);

sub _check_http
{
	print "checking http\n";
	my $data = shift;
	#print $data;
	foreach my $check (@http_checks) {
		if ($data =~ /$check/moi) {
			print "match on $check!\n";
		}
	}
	#print $data;
	return 1;
}

sub cb
{
	my ($payload) = @_;

	if ($payload) {
		print "\n";

		my $ip_obj = NetPacket::IP->decode($payload->get_data());
		

		if($ip_obj->{proto} == IP_PROTO_TCP) {
			print "TCP Id: " . $payload->swig_id_get() . "\n";
			# decode the TCP header
			my $tcp_obj = NetPacket::TCP->decode($ip_obj->{data});

			if ($tcp_obj->{flags} & NetPacket::TCP::SYN) {
				#print("$ip_obj->{src_ip} => $ip_obj->{dest_ip} $ip_obj->{proto}\n");
				#print "TCP src_port: $tcp_obj->{src_port}\n";
				#print "TCP dst_port: $tcp_obj->{dest_port}\n";
				#print "TCP flags   : $tcp_obj->{flags}\n";
			}
			elsif ($tcp_obj->{flags} & NetPacket::TCP::PSH) {
				print $tcp_obj->{src_port};
				if ($tcp_obj->{data} =~ /HTTP\/1.1\ 200\ OK/) {
					print "TCP HEADER data:\n";
					print "*" x 50 . "\n";
					print $tcp_obj->{data};
					print "*" x 50 . "\n";
					if ($tcp_obj->{src_port} == 80) {
						_check_http($tcp_obj->{data}) or return $payload->set_verdict($nfqueue::NF_DROP);
					}
				}
			}
		}

		print "\n";
		$payload->set_verdict($nfqueue::NF_ACCEPT);
	}
}


$q = new nfqueue::queue();

$SIG{INT} = "cleanup";

print "setting callback\n";
$q->set_callback(\&cb);

print "open\n";
$q->fast_open(0, AF_INET);

print "trying to run\n";
$q->try_run();
