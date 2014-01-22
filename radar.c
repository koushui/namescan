
#include <radar.h>
#include <stdio.h>
#include <stdlib.h>
#include <fingerprint.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <arpa/inet.h>

extern unsigned probesize;

void process_pkt(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes);

void radar_set_defaults(radar_params_t* rp)
{
    rp->dev = NULL;
    rp->outfile = NULL;
  	rp->level = 0;
}

pcap_t* radar_init(radar_params_t* rp)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;
    bpf_u_int32 mask;
    bpf_u_int32 net;

    if (rp->dev == NULL) {
        rp->dev = pcap_lookupdev(errbuf);
        if (rp->dev == NULL) {
            LOG_ERROR("Can't lookup device: %s\n", errbuf);
            return NULL;
        }
    }

    pcap_t* handle;
    handle = pcap_open_live(rp->dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        LOG_ERROR("Can't open live %s: %s\n", rp->dev, errbuf);
        return NULL;
    }

    if (pcap_lookupnet(rp->dev, &net, &mask, errbuf) == -1) {
        LOG_ERROR("Couldn't get netmask for device %s: %s\n", rp->dev, errbuf);
        return NULL;
    }

    LOG_DEBUG("Working on %s\n", rp->dev);

    if (pcap_compile(handle, &fp, "src port 53", 0, net) == -1) {
        fprintf(stderr, "Couldn't parse filter: %s\n", pcap_geterr(handle));
        return NULL;
    }

    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter: %s\n", pcap_geterr(handle));
        exit(2);
    }

    return handle;
}

void* radar(void* p)
{
    radar_params_t* rp = (radar_params_t*)p;
    pcap_t* handle = rp->handle;
    pcap_loop(handle, -1, process_pkt, (u_char*)rp);
    return NULL;
}

void process_pkt(u_char* args, const struct pcap_pkthdr* h, const u_char* packet)
{
    radar_params_t* rp = (radar_params_t*)args;

    struct ip* ip = (struct ip*)(packet + sizeof(struct ether_header));
    struct udphdr* udphdr = (struct udphdr*)(packet + sizeof(struct ether_header)
        + sizeof(struct ip));
    const u_char* dns = packet + sizeof(struct ether_header) + sizeof(struct ip)
        + sizeof(struct udphdr);

    char buf[INET_ADDRSTRLEN];
    float ratio = 0;

    // is a fragment 
    if (ip->ip_off & IP_MF) {
        printf("GOT A FRAGMENT\n");
    }
    
    // is the last fragment
    if (!(ip->ip_off & IP_MF) && (ip->ip_off & IP_OFFMASK)) {
        printf("GOT THE LAST FRAGMENT");
    }
    
    // not a fragment
    if (!(ip->ip_off & IP_MF)) {
        ratio = (float)h->len/(float)probesize;
    }

    if (ratio >= rp->level && fingerprint_check(udphdr->dest, *(uint16_t*)dns)) {
        LOG_INFO("%c[2K", 27);
        LOG_INFO("\rResponse from %s, ", inet_ntop(AF_INET, &ip->ip_src, buf, INET_ADDRSTRLEN));
            LOG_INFO("amp ratio: %.2f\n", ratio);
        fflush(stdout);
        if (rp->outfile != NULL) {
            fprintf(rp->outfile, "%s\n", buf);
            fflush(rp->outfile);
        }
    } else {
        LOG_DEBUG("Ignoring packet from %s\n", inet_ntop(AF_INET, &ip->ip_src, buf, INET_ADDRSTRLEN));
    }
}
