// Simple pcap-based network handler

// Let pcap use BSD types: https://stackoverflow.com/questions/15393905/c-pcap-library-unknown-types-error
#define _GNU_SOURCE

#include "util.h"
#include <pcap.h>
#include <string.h>

static pcap_t* pcap_adhandle;

int net_init(char* netarg)
{
    pcap_if_t *devlist, *temp;
    char error[PCAP_ERRBUF_SIZE];
    if (pcap_findalldevs(&devlist, error) < 0) {
        FATAL("NET", "pcap_findalldevs: %s\n", error);
        return -1;
    }

    int i = 1;
    if (!netarg)
        LOG("NET", " == List of network devices ==\n");
    char* intf = NULL;

    for (temp = devlist; temp; temp = temp->next) {
        if (netarg) {
            if (strcmp(temp->name, netarg) == 0) {
                intf = temp->name;
                break;
            }
        } else {
            // No argument provided, list them out
            LOG("NET", "%d: %s (%s)\n", i, temp->name, temp->description ? temp->description : NULL);
        }
        i++;
    }

    if (!netarg) {
        LOG("NET", "Network devices listed. Re-run with network device specified\n");
        exit(1);
    }

    if (!intf) {
        LOG("NET", "intf not found: %s\n", netarg);
        pcap_freealldevs(devlist);
        return -1;
    }
    pcap_adhandle = pcap_open_live(intf, 65536, 0, 0, error);
    if (!pcap_adhandle) {
        LOG("NET", "Failed to open pcap interface: %s\n", error);
        pcap_freealldevs(devlist);
        return -1;
    }
    pcap_freealldevs(devlist);

    if (pcap_setnonblock(pcap_adhandle, 1, error) < 0) {
        LOG("NET", "Unable to set non-blocking mode\n");
        return -1;
    }
    return 0;
}

int net_send(void* req, int reqlen)
{
    LOG("NET", "Sending %d bytes over the network\n", reqlen);
    if (pcap_sendpacket(pcap_adhandle, req, reqlen) < 0) {
        LOG("NET", "Unable to send frame (data= ((uint8_t*)%p) len=%d)\n", req, reqlen);
        return -1;
    }
    return 0;
}
static void (*recv_cb)(void* data, int len);
static void pcap_recv(u_char* param, const struct pcap_pkthdr* header, const u_char* pkt_data)
{
    UNUSED(param);
    printf("packet recv: len=%d data=%p\n", header->caplen, pkt_data);
    recv_cb((void*)pkt_data, header->caplen);
}

// Poll pcap network device
void net_poll(void (*cb)(void* data, int len))
{
    recv_cb = cb;
    int retv = pcap_dispatch(pcap_adhandle, 1, pcap_recv, NULL);
    if (retv < 0)
        FATAL("NET", "Failed to poll for packets\n");
    else if (retv == 0) { // ?
    }
    else { // retv > 0, ??
    }
}