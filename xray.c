#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <libmnl/libmnl.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/types.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <regex.h> 

static struct mnl_socket *nl;
regex_t regexIn;

static struct nlmsghdr *nfq_hdr_put(char *buf, int type, uint32_t queue_num) {
	struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= (NFNL_SUBSYS_QUEUE << 8) | type;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	
	struct nfgenmsg *nfg = mnl_nlmsg_put_extra_header(nlh, sizeof(*nfg));
	nfg->nfgen_family = AF_UNSPEC;
	nfg->version = NFNETLINK_V0;
	nfg->res_id = htons(queue_num);
	
	return nlh;
}

static void nfq_send_verdict(int queue_num, uint32_t id) {
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	struct nlattr *nest;
	
	nlh = nfq_hdr_put(buf, NFQNL_MSG_VERDICT, queue_num);
	nfq_nlmsg_verdict_put(nlh, id, NF_ACCEPT);
	
	/* example to set the connmark. First, start NFQA_CT section: */
	nest = mnl_attr_nest_start(nlh, NFQA_CT);
	
	/* then, add the connmark attribute: */
	mnl_attr_put_u32(nlh, CTA_MARK, htonl(42));
	/* more conntrack attributes, e.g. CTA_LABEL, could be set here */
	
	/* end conntrack section */
	mnl_attr_nest_end(nlh, nest);
	
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
}

static int queue_cb(const struct nlmsghdr *nlh, void *data)
{
	struct nfqnl_msg_packet_hdr *ph = NULL;
	struct nlattr *attr[NFQA_MAX+1] = {};
	uint32_t id = 0, skbinfo;
	struct nfgenmsg *nfg;
	uint16_t plen;
	
	if (nfq_nlmsg_parse(nlh, attr) < 0) {
		perror("problems parsing");
		return MNL_CB_ERROR;
	}
	
	nfg = mnl_nlmsg_get_payload(nlh);
	
	if (attr[NFQA_PACKET_HDR] == NULL) {
		fputs("metaheader not set\n", stderr);
		return MNL_CB_ERROR;
	}
	
	ph = mnl_attr_get_payload(attr[NFQA_PACKET_HDR]);
	plen = mnl_attr_get_payload_len(attr[NFQA_PAYLOAD]);
	skbinfo = attr[NFQA_SKB_INFO] ? ntohl(mnl_attr_get_u32(attr[NFQA_SKB_INFO])) : 0;
	
	if (attr[NFQA_CAP_LEN]) {
		uint32_t orig_len = ntohl(mnl_attr_get_u32(attr[NFQA_CAP_LEN]));
		if (orig_len != plen)
			printf("truncated ");
	}
	
	if (skbinfo & NFQA_SKB_GSO)
		printf("GSO ");
	
	id = ntohl(ph->packet_id);
	printf("packet received (id=%u hw=0x%04x hook=%u, payload len %u",
		   id, ntohs(ph->hw_protocol), ph->hook, plen);
	
	/*
	 * ip/tcp checksums are not yet valid, e.g. due to GRO/GSO.
	 * The application should behave as if the checksums are correct.
	 *
	 * If these packets are later forwarded/sent out, the checksums will
	 * be corrected by kernel/hardware.
	 */
	if (skbinfo & NFQA_SKB_CSUMNOTREADY)
		printf(", checksum not ready");
	puts(")");

	if (attr[NFQA_PAYLOAD]) {
		
		unsigned char *pkt = mnl_attr_get_payload(attr[NFQA_PAYLOAD]);
		struct iphdr *ip_ptr = (struct iphdr *)((u_int8_t *) pkt);
		u_int8_t ip_len = (ip_ptr->ihl * 4);
		struct tcphdr *tcp_ptr = (struct tcphdr *)((u_int8_t *) ip_ptr + ip_len);

		if (tcp_ptr->doff != 0) {
			uint16_t pkt_size = mnl_attr_get_payload_len(attr[NFQA_PAYLOAD]);
			unsigned char *payload_ptr = (u_int8_t *) tcp_ptr + (tcp_ptr->doff * sizeof(u_int32_t));
			uint16_t payload_len = pkt_size - (ip_len + tcp_ptr->doff * 4);

			if (payload_len > 0) {
				char subbuff[payload_len];
				memcpy( subbuff, payload_ptr, payload_len - 1 );
				subbuff[payload_len] = '\0';
				int regexRet = regexec(&regexIn, subbuff, 0, NULL, 0);
				if (!regexRet) {
					printf("HEADER (%i bytes) :: \n__________\n%s\n__________\n", payload_len, subbuff);
				} else if (regexRet != REG_NOMATCH) {
					char msgbuf[100];
					regerror(regexRet, &regexIn, msgbuf, sizeof(msgbuf));
					fprintf(stderr, "Regex match failed: %s\n", msgbuf);
				}

			}
		}

	}

	//printf("sending verdict to queue handle %i\n", nfg->res_id);
	nfq_send_verdict(ntohs(nfg->res_id), id);
	//puts("verdict sent");
	return MNL_CB_OK;
}

void setupInboundFilter(unsigned int queue_num) {
	
	char *buf;
	/* largest possible packet payload, plus netlink data overhead: */
	size_t sizeof_buf = 0xffff + (MNL_SOCKET_BUFFER_SIZE/2);
	struct nlmsghdr *nlh;
	int ret;
	unsigned int portid;

	nl = mnl_socket_open(NETLINK_NETFILTER);
	if (nl == NULL) {
		perror("mnl_socket_open");
		exit(EXIT_FAILURE);
	}
	
	if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
		perror("mnl_socket_bind");
		exit(EXIT_FAILURE);
	}
	portid = mnl_socket_get_portid(nl);
	
	buf = malloc(sizeof_buf);
	if (!buf) {
		perror("allocate receive buffer");
		exit(EXIT_FAILURE);
	}
	
	/* PF_(UN)BIND is not needed with kernels 3.8 and later */
	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, 0);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_PF_UNBIND);
	
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
	
	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, 0);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_PF_BIND);
	
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
	
	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, queue_num);
	nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_BIND);
	
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
	
	nlh = nfq_hdr_put(buf, NFQNL_MSG_CONFIG, queue_num);
	nfq_nlmsg_cfg_put_params(nlh, NFQNL_COPY_PACKET, 0xffff);
	
	mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_GSO));
	mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_GSO));
	
	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
		perror("mnl_socket_send");
		exit(EXIT_FAILURE);
	}
	
	/* ENOBUFS is signalled to userspace when packets were lost
	 * on kernel side.  In most cases, userspace isn't interested
	 * in this information, so turn it off.
	 */
	ret = 1;
	mnl_socket_setsockopt(nl, NETLINK_NO_ENOBUFS, &ret, sizeof(int));
	
	for (;;) {
		ret = mnl_socket_recvfrom(nl, buf, sizeof_buf);
		if (ret == -1) {
			perror("mnl_socket_recvfrom");
			exit(EXIT_FAILURE);
		}
		
		ret = mnl_cb_run(buf, ret, 0, portid, queue_cb, NULL);
		if (ret < 0){
			perror("mnl_cb_run");
			exit(EXIT_FAILURE);
		}
	}
	
	mnl_socket_close(nl);
}

void help(char *self) {
	fprintf(stderr, "Usage: %s [-iod]\n", self);
	puts("-i [inbound netfilter queue number]");
	puts("-o [outbound netfilter queue number]");
	puts("-d run as a daemon");
}

int main(int argc, char *argv[]) {

	unsigned int inboundQueue = -1;
	unsigned int outboundQueue = -1;
	bool daemon = false;
	int opt;
	printf("starting up %s\n", argv[0]);
	while ((opt = getopt(argc, argv, ":i:o:d:h")) != -1) {
		switch (opt) {
			case 'i': 
				printf("setting inbound queue to %s\n", optarg);
				inboundQueue = atoi(optarg);
				break;
			case 'o': 
				printf("setting outbound queue to %s\n", optarg);
				outboundQueue = atoi(optarg);
				break;
			case 'd': 
				puts("enabling daemon mode\n");
				daemon = true; 
				break;
			case 'h':
				help(argv[0]);
				exit(EXIT_SUCCESS);
			default:
				help(argv[0]);
				exit(EXIT_FAILURE);
		}
	};
	if (inboundQueue < 0 || outboundQueue < 0) {
		help(argv[0]);
		exit(EXIT_FAILURE);
	}

	int regexRet = regcomp(&regexIn, "^(GET|POST|PUT|DELETE|PATCH|PURGE|BAN|HEAD|OPTIONS) [^\\s]* HTTP\\/1\\.1\\s", REG_EXTENDED);
	if (regexRet) {
		fprintf(stderr, "Could not compile inbound regex\n");
		exit(EXIT_FAILURE);
	}

	printf("%s running....\n", argv[0]);

	setupInboundFilter(inboundQueue);
	regfree(&regexIn);
	return 0;
}
