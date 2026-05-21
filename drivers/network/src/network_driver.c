#include "network_driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Network device table */
static NetworkDevice devices[MAX_NETWORK_DEVICES];
static int device_count = 0;

/* ============================================================================ */
/* Ethernet Driver */
/* ============================================================================ */

int eth_init() {
    printf("Ethernet Driver initialized\n");
    
    /* Create eth0 device */
    if (device_count < MAX_NETWORK_DEVICES) {
        NetworkDevice *dev = &devices[device_count++];
        strcpy(dev->name, "eth0");
        dev->type = NET_DRIVER_ETHERNET;
        dev->status = NET_STATUS_DISCONNECTED;
        dev->mac[0] = 0x00;
        dev->mac[1] = 0x11;
        dev->mac[2] = 0x22;
        dev->mac[3] = 0x33;
        dev->mac[4] = 0x44;
        dev->mac[5] = 0x55;
    }
    
    return 0;
}

int eth_enable_device(const char *name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            devices[i].status = NET_STATUS_CONNECTED;
            printf("Ethernet device '%s' enabled\n", name);
            return 0;
        }
    }
    return -1;
}

int eth_disable_device(const char *name) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            devices[i].status = NET_STATUS_DISCONNECTED;
            printf("Ethernet device '%s' disabled\n", name);
            return 0;
        }
    }
    return -1;
}

int eth_send_packet(const char *device, EthernetFrame *frame) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            if (devices[i].status != NET_STATUS_CONNECTED) {
                printf("Error: Device '%s' not connected\n", device);
                return -1;
            }
            
            devices[i].packets_sent++;
            devices[i].bytes_sent += frame->payload_len;
            return 0;
        }
    }
    return -1;
}

int eth_receive_packet(const char *device, EthernetFrame *frame) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            if (devices[i].status != NET_STATUS_CONNECTED) {
                return -1;
            }
            
            devices[i].packets_received++;
            return 0;
        }
    }
    return -1;
}

/* ============================================================================ */
/* WiFi Driver */
/* ============================================================================ */

int wifi_init() {
    printf("WiFi Driver initialized\n");
    
    /* Create wlan0 device */
    if (device_count < MAX_NETWORK_DEVICES) {
        NetworkDevice *dev = &devices[device_count++];
        strcpy(dev->name, "wlan0");
        dev->type = NET_DRIVER_WIFI;
        dev->status = NET_STATUS_DISCONNECTED;
    }
    
    return 0;
}

int wifi_scan(const char *device) {
    printf("Scanning for WiFi networks on '%s'...\n", device);
    
    /* Simulated scan results */
    printf("  1. RouterNetwork (Excellent Signal, Secured)\n");
    printf("  2. GuestWiFi (Good Signal, Open)\n");
    printf("  3. NeighborNet (Fair Signal, Secured)\n");
    printf("  4. PublicWiFi (Excellent Signal, Open)\n");
    
    return 4;  /* Number of networks found */
}

int wifi_connect(const char *device, const char *ssid, const char *password) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            if (devices[i].type != NET_DRIVER_WIFI) {
                printf("Error: Device '%s' is not a WiFi device\n", device);
                return -1;
            }
            
            devices[i].status = NET_STATUS_CONNECTING;
            printf("Connecting to WiFi network '%s'...\n", ssid);
            
            /* Simulate connection delay */
            for (int j = 0; j < 10; j++) {
                printf(".");
                fflush(stdout);
                for (int k = 0; k < 50000000; k++) {
                    __asm__("nop");
                }
            }
            printf("\n");
            
            devices[i].status = NET_STATUS_CONNECTED;
            printf("Successfully connected to '%s'\n", ssid);
            
            return 0;
        }
    }
    return -1;
}

int wifi_disconnect(const char *device) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            if (devices[i].type != NET_DRIVER_WIFI) {
                return -1;
            }
            
            devices[i].status = NET_STATUS_DISCONNECTED;
            printf("Disconnected from WiFi network\n");
            return 0;
        }
    }
    return -1;
}

int wifi_get_signal_strength(const char *device) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            if (devices[i].status == NET_STATUS_CONNECTED) {
                return 85;  /* 0-100 scale */
            }
        }
    }
    return 0;
}

/* ============================================================================ */
/* Network Configuration */
/* ============================================================================ */

int net_configure_ip(const char *device, const char *ip, const char *gateway, const char *netmask) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            /* Parse IP address (simplified) */
            unsigned int a, b, c, d;
            sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d);
            devices[i].config.ip_address = (a << 24) | (b << 16) | (c << 8) | d;
            
            sscanf(gateway, "%u.%u.%u.%u", &a, &b, &c, &d);
            devices[i].config.gateway = (a << 24) | (b << 16) | (c << 8) | d;
            
            sscanf(netmask, "%u.%u.%u.%u", &a, &b, &c, &d);
            devices[i].config.netmask = (a << 24) | (b << 16) | (c << 8) | d;
            
            printf("Network configuration updated for '%s'\n", device);
            printf("  IP: %s\n", ip);
            printf("  Gateway: %s\n", gateway);
            printf("  Netmask: %s\n", netmask);
            
            return 0;
        }
    }
    return -1;
}

int net_get_config(const char *device, NetworkConfig *config) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            *config = devices[i].config;
            return 0;
        }
    }
    return -1;
}

int net_set_dns(const char *device, const char *dns1, const char *dns2) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            unsigned int a, b, c, d;
            sscanf(dns1, "%u.%u.%u.%u", &a, &b, &c, &d);
            devices[i].config.dns1 = (a << 24) | (b << 16) | (c << 8) | d;
            
            sscanf(dns2, "%u.%u.%u.%u", &a, &b, &c, &d);
            devices[i].config.dns2 = (a << 24) | (b << 16) | (c << 8) | d;
            
            printf("DNS servers configured: %s, %s\n", dns1, dns2);
            return 0;
        }
    }
    return -1;
}

/* ============================================================================ */
/* Network Status */
/* ============================================================================ */

NetworkStatus net_get_status(const char *device) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            return devices[i].status;
        }
    }
    return NET_STATUS_ERROR;
}

void net_print_stats(const char *device) {
    for (int i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, device) == 0) {
            printf("Network Statistics for '%s':\n", device);
            printf("  Status: %s\n", (devices[i].status == NET_STATUS_CONNECTED) ? "Connected" : "Disconnected");
            printf("  Packets Sent: %llu\n", devices[i].packets_sent);
            printf("  Packets Received: %llu\n", devices[i].packets_received);
            printf("  Bytes Sent: %llu\n", devices[i].bytes_sent);
            printf("  Bytes Received: %llu\n", devices[i].bytes_received);
            printf("  Errors: %llu\n", devices[i].errors);
            return;
        }
    }
}
