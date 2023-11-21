#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poll.h>

#include "portsentry.h"
#include "listener.h"
#include "config_data.h"
#include "io.h"
#include "util.h"
#include "device.h"

#define BUFFER_TIMEOUT 2000

pcap_t *PcapOpenLiveImmediate(const char *source, int snaplen, int promisc, int to_ms, char *errbuf);
static uint8_t CreateAndAddDevice(struct ListenerModule *lm, const char *name);
static int AutoPrepDevices(struct ListenerModule *lm, uint8_t includeLo);
static int PrepDevices(struct ListenerModule *lm);
// static int GetNoDevices(struct ListenerModule *lm);

/* Heavily inspired by src/lib/libpcap/pcap-bpf.c from OpenBSD's pcap implementation.
 * We must use pcap_create() and pcap_activate() instead of pcap_open_live() because
 * we need to set the immediate mode flag, which can only be done on an unactivated
 * pcap_t.
 *
 * OpenBSD's libpcap implementation require immediate mode and non-blocking socket
 * in order for poll() (and select()/kevent()) to work properly. This approach works
 * for other Unixes as well, so it's no harm in doing it this way. Using immediate mode
 * with a non-blocking fd makes pcap a bit snappier anyway so it's a win-win.
 * */
pcap_t *PcapOpenLiveImmediate(const char *source, int snaplen, int promisc, int to_ms, char *errbuf) {
  pcap_t *p;
  int status;

  if ((p = pcap_create(source, errbuf)) == NULL)
    return (NULL);
  if ((status = pcap_set_snaplen(p, snaplen)) < 0)
    goto fail;
  if ((status = pcap_set_promisc(p, promisc)) < 0)
    goto fail;
  if ((status = pcap_set_timeout(p, to_ms)) < 0)
    goto fail;
  if ((status = pcap_set_immediate_mode(p, 1)) < 0)
    goto fail;

  if ((status = pcap_activate(p)) < 0)
    goto fail;
  return (p);
fail:
  if (status == PCAP_ERROR)
    fprintf(stderr, "%s: %s", source, pcap_geterr(p));
  else if (status == PCAP_ERROR_NO_SUCH_DEVICE ||
           status == PCAP_ERROR_PERM_DENIED ||
           status == PCAP_ERROR_PROMISC_PERM_DENIED)
    fprintf(stderr, "%s: %s (%s)", source,
            pcap_statustostr(status), pcap_geterr(p));
  else
    fprintf(stderr, "%s: %s", source,
            pcap_statustostr(status));
  pcap_close(p);
  return (NULL);
}

static uint8_t CreateAndAddDevice(struct ListenerModule *lm, const char *name) {
  struct Device *dev;

  assert(lm != NULL);

  if (FindDeviceByName(lm, name) == TRUE) {
    Error("Device %s appears twice", name);
    return FALSE;
  }

  if ((dev = CreateDevice(name)) == NULL) {
    Error("Unable to allocate memory for device %s", name);
    return FALSE;
  }

  if (AddDevice(lm, dev) == FALSE) {
    Error("Unable to add device %s", name);
    FreeDevice(dev);
    return FALSE;
  }

  return TRUE;
}

static int AutoPrepDevices(struct ListenerModule *lm, uint8_t includeLo) {
  pcap_if_t *alldevs, *d;
  char errbuf[PCAP_ERRBUF_SIZE];

  if (pcap_findalldevs(&alldevs, errbuf) == PCAP_ERROR) {
    Error("Unable to retrieve network interfaces: %s", errbuf);
    return FALSE;
  }

  for (d = alldevs; d != NULL; d = d->next) {
    if (includeLo == FALSE && ((d->flags & PCAP_IF_LOOPBACK) != 0)) {
      continue;
    }

    Verbose("Adding device %s", d->name);
    if (CreateAndAddDevice(lm, d->name) == FALSE) {
      Error("Unable to add device %s, skipping", d->name);
    }
  }

  pcap_freealldevs(alldevs);
  return TRUE;
}

static int PrepDevices(struct ListenerModule *lm) {
  int i;

  assert(lm != NULL);
  assert(strlen(configData.interfaces[0]) > 0);

  if (strncmp(configData.interfaces[0], "ALL_NLO", (IF_NAMESIZE - 1)) == 0) {
    if (AutoPrepDevices(lm, FALSE) == FALSE) {
      return FALSE;
    }
  } else if (strncmp(configData.interfaces[0], "ALL", (IF_NAMESIZE - 1)) == 0) {
    if (AutoPrepDevices(lm, TRUE) == FALSE) {
      return FALSE;
    }
  } else {
    i = 0;
    while (strlen(configData.interfaces[i]) > 0) {
      if (CreateAndAddDevice(lm, configData.interfaces[i]) == FALSE) {
        Error("Unable to add device %s, skipping", configData.interfaces[i]);
      }
      i++;
    }
  }

  if (lm->root == NULL) {
    Error("No network devices could be added");
    return FALSE;
  }

  return TRUE;
}

int GetNoDevices(const struct ListenerModule *lm) {
  int count;
  struct Device *current;

  assert(lm != NULL);

  count = 0;
  current = lm->root;
  while (current != NULL) {
    count++;
    current = current->next;
  }

  return count;
}

struct ListenerModule *AllocListenerModule(void) {
  struct ListenerModule *lm;

  lm = malloc(sizeof(struct ListenerModule));
  if (lm == NULL) {
    Error("Unable to allocate memory for listener module");
    return NULL;
  }

  memset(lm, 0, sizeof(struct ListenerModule));

  return lm;
}

void FreeListenerModule(struct ListenerModule *lm) {
  struct Device *current;

  if (lm == NULL) {
    return;
  }

  current = lm->root;
  while (current != NULL) {
    current = current->next;
    FreeDevice(current);
  }

  free(lm);
}

int InitListenerModule(struct ListenerModule *lm) {
  char errbuf[PCAP_ERRBUF_SIZE];
  struct Device *current, *next;

  if (PrepDevices(lm) == FALSE) {
    return FALSE;
  }

  current = lm->root;
  while (current != NULL) {
    next = current->next;

    if (pcap_lookupnet(current->name, &current->net, &current->mask, errbuf) < 0) {
      Error("Unable to retrieve network/netmask for device %s, skipping", current->name);
      RemoveDevice(lm, current);
      goto next;
    }

    if ((current->handle = PcapOpenLiveImmediate(current->name, BUFSIZ, 0, BUFFER_TIMEOUT, errbuf)) == NULL) {
      Error("Couldn't open device %s: %s", current->name, errbuf);
      RemoveDevice(lm, current);
      goto next;
    }

    if (pcap_setnonblock(current->handle, 1, errbuf) < 0) {
      Error("Unable to set pcap_setnonblock on %s: %s", current->name, errbuf);
      RemoveDevice(lm, current);
      goto next;
    }

    if (pcap_setdirection(current->handle, PCAP_D_IN) < 0) {
      Error("Couldn't set direction on %s: %s", current->name, pcap_geterr(current->handle));
      RemoveDevice(lm, current);
      goto next;
    }

    if (pcap_datalink(current->handle) != DLT_EN10MB) {
      Error("Device %s doesn't provide Ethernet headers, ignoring", current->name);
      RemoveDevice(lm, current);
      goto next;
    }

    if ((current->fd = pcap_get_selectable_fd(current->handle)) < 0) {
      Error("Couldn't get file descriptor on device %s: %s", current->name, pcap_geterr(current->handle));
      RemoveDevice(lm, current);
      goto next;
    }

    // TODO: Setup filter on device

  next:
    current = next;
  }

  if (lm->root == NULL) {
    Error("No network devices could be initiated, stopping");
    return FALSE;
  }

  current = lm->root;
  while (current != NULL) {
    Verbose("Device %s ready", current->name);
    current = current->next;
  }

  return TRUE;
}

uint8_t AddDevice(struct ListenerModule *lm, struct Device *add) {
  struct Device *current;

  if (lm == NULL || add == NULL) {
    return FALSE;
  }

  if (FindDeviceByName(lm, add->name) == TRUE) {
    Verbose("Device %s already specified", add->name);
    return FALSE;
  }

  if (lm->root == NULL) {
    lm->root = add;
  } else {
    current = lm->root;
    while (current->next != NULL) {
      current = current->next;
    }

    current->next = add;
  }

  return TRUE;
}

uint8_t RemoveDevice(struct ListenerModule *lm, struct Device *remove) {
  struct Device *current, *previous;

  if (lm == NULL || remove == NULL) {
    return FALSE;
  }

  current = lm->root;
  previous = NULL;
  while (current != NULL) {
    if (current == remove) {
      if (previous == NULL) {
        lm->root = current->next;
      } else {
        previous->next = current->next;
      }

      FreeDevice(current);
      return TRUE;
    }

    previous = current;
    current = current->next;
  }

  return FALSE;
}

uint8_t FindDeviceByName(struct ListenerModule *lm, const char *name) {
  struct Device *current;

  if (lm == NULL) {
    return FALSE;
  }

  if (strlen(name) > (IF_NAMESIZE - 1)) {
    return FALSE;
  }

  current = lm->root;
  while (current != NULL) {
    if (strncmp(current->name, name, (IF_NAMESIZE - 1)) == 0) {
      return TRUE;
    }

    current = current->next;
  }

  return FALSE;
}

struct pollfd *SetupPollFds(const struct ListenerModule *lm, int *nfds) {
  struct pollfd *fds = NULL;
  struct Device *current = NULL;
  int i = 0;

  if ((fds = malloc(sizeof(struct pollfd) * GetNoDevices(lm))) == NULL) {
    Error("Unable to allocate memory for pollfd");
    return NULL;
  }

  current = lm->root;
  while (current != NULL) {
    fds[i].fd = current->fd;
    fds[i].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
    fds[i].revents = 0;
    current = current->next;
    i++;
  }

  *nfds = i;

  return fds;
}

struct Device *GetDeviceByFd(const struct ListenerModule *lm, const int fd) {
  for (struct Device *current = lm->root; current != NULL; current = current->next) {
    if (current->fd == fd) {
      return current;
    }
  }

  return NULL;
}

#if 0
void printPacket(const u_char *packet, const struct pcap_pkthdr *header) {
  /*
    struct iphdr *iphdr;
    struct in_addr saddr, daddr;
    struct ether_header *eptr;
    struct tcphdr *tcphdr;
    struct udphdr *udphdr;
    int iplen;
    char csaddr[16], cdaddr[16];
  */
  fprintf(stderr, "Jacked a packet %p with length of %d [%d]\n", (void *)packet, header->caplen, header->len);
  /*
  eptr = (struct ether_header *)packet;
  iphdr = (struct iphdr *)(packet + sizeof(struct ether_header));
  printf("Source Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
         eptr->ether_shost[0], eptr->ether_shost[1], eptr->ether_shost[2],
         eptr->ether_shost[3], eptr->ether_shost[4], eptr->ether_shost[5]);
  printf("Destination Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
         eptr->ether_dhost[0], eptr->ether_dhost[1], eptr->ether_dhost[2],
         eptr->ether_dhost[3], eptr->ether_dhost[4], eptr->ether_dhost[5]);
  printf("Ethernet type hex:%x dec:%d\n", ntohs(eptr->ether_type),
         ntohs(eptr->ether_type));

  saddr.s_addr = iphdr->saddr;
  daddr.s_addr = iphdr->daddr;
  snprintf(csaddr, sizeof(csaddr), "%s", inet_ntoa(saddr));
  snprintf(cdaddr, sizeof(cdaddr), "%s", inet_ntoa(daddr));
  printf("ihl: %d proto: %s ver: %d saddr: %s daddr: %s\n", iphdr->ihl,
         iphdr->protocol == IPPROTO_TCP   ? "tcp"
         : iphdr->protocol == IPPROTO_UDP ? "udp"
                                          : "other",
         iphdr->version, csaddr, cdaddr);

  iplen = iphdr->ihl * 4;
  printf("iplen: %d\n", iplen);

  if (iphdr->protocol == IPPROTO_TCP) {
    tcphdr = (struct tcphdr *)(packet + sizeof(struct ether_header) +
                               iplen);
    printf("sport: %d dport: %d\n", ntohs(tcphdr->th_sport),
           ntohs(tcphdr->th_dport));
  } else if (iphdr->protocol == IPPROTO_UDP) {
    udphdr = (struct udphdr *)(packet + sizeof(struct ether_header) +
                               iplen);
    printf("sport: %d dport: %d\n", ntohs(udphdr->uh_sport),
           ntohs(udphdr->uh_dport));
  }
*/
  printf("\n");
}
#endif

void HandlePacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
  (void)args;
  fprintf(stderr, "Jacked a packet %p with length of %d [%d]\n", (void *)packet, header->caplen, header->len);
}
