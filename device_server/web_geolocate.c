#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <curl/curl.h>
#include "string.h"
#include "float.h"
#include "web_geolocate.h"
#include "util.h"
#include "config.h"
//just a static sized buffer is enough here, we're not looking to retrieve the world
//no need for memory allocation etc.
typedef struct  {
    char response[BUF_SIZE];
    size_t size;
} memory;

static size_t cb(void * data, size_t size, size_t nmemb, void * userp) {
    size_t realsize = size * nmemb;
    memory * mem = ( memory *)userp;

    if (mem->size + size > BUF_SIZE) {
        return 0;
    }

    memcpy(&(mem->response[mem->size]), data, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;
    return realsize;
}

location_result do_geolocate( char * url, char * query) {
    CURL * curl;
    CURLcode res;
    location_result ret = {0, 0, 0, 0};
    memory chunk = {{0}, 0};
    curl = curl_easy_init();

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
        struct curl_slist * headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, query);
        /* send all data to this function  */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
        /* we pass our 'chunk' struct to the callback function */
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));

        /* always cleanup */
        curl_easy_cleanup(curl);
    }

    if (chunk.size) {
        char * latPtr = strstr(chunk.response, "\"lat\":") ;
        char * longPtr = strstr(chunk.response, "\"lng\":") ;
        char * accuracyPtr = strstr(chunk.response, "\"accuracy\":") ;

        if (latPtr > 0 && longPtr > 0) {
            latPtr += 6;
            longPtr += 6;
            accuracyPtr += 11;
            ret.lat = parse_float(latPtr);
            ret.lng = parse_float(longPtr);
            ret.radius = parse_int(accuracyPtr, 6);
            ret.valid = true;
        }
    }

    return ret;
}

location_result here_geolocate_wifi(wifi_network * network, size_t network_count) {
    char postData[BUF_SIZE] = "{\"wlan\": [";
    char hex[3] = {0, 0, 0};

    for (size_t i = 0; i < network_count; i++) {
        strcat(postData, "{\"mac\": \"");

        for (size_t n = 0; n < 6; n++) {
            CONVERT_HEX(network[i].mac_addr[n], hex[0], hex[1]);
            hex[2] = 0;
            strcat(postData, hex);

            if (n < 5) {
                strcat(postData, ":");
            }
        }

        strcat(postData, "\"");
        strcat(postData, "}");

        if (i < (network_count - 1)) {
            strcat(postData, ",");
        }
    }

    strcat(postData, "]}");
    char url[BUF_SIZE] = "https://positioning.hereapi.com/v2/locate?fallback=singleWifi&apiKey=";
    strcat(url, HERE_API_KEY);
    return do_geolocate(url, postData);
}

location_result google_geolocate_wifi(wifi_network * network, size_t network_count) {
    char postData[BUF_SIZE] = "{\"considerIp\": false,\"wifiAccessPoints\":[";
    char hex[3] = {0, 0, 0};

    for (size_t i = 0; i < network_count; i++) {
        strcat(postData, "{\"macAddress\":\"");

        for (size_t n = 0; n < 6; n++) {
            CONVERT_HEX(network[i].mac_addr[n], hex[0], hex[1]);
            hex[2] = 0;
            strcat(postData, hex);

            if (n < 5) {
                strcat(postData, ":");
            }
        }

        strcat(postData, "\"");
        strcat(postData, "}");

        if (i < (network_count - 1)) {
            strcat(postData, ",");
        }
    }

    strcat(postData, "]}");
    char url[BUF_SIZE] = "https://www.googleapis.com/geolocation/v1/geolocate?fallback=any&key=";
    strcat(url, GOOGLE_API_KEY);
    return  do_geolocate(url, postData);
}

//location only for LTE so far.
location_result here_geolocate_gsm(cell_tower * tower) {
    char postData[BUF_SIZE] = "{\"gsm\": [";
    char hex[3] = {0, 0, 0};
    strcat(postData, "{\"mcc\":");
    char data[16] = {0};
    sprintf(data, "%i", tower->mcc);
    strcat(postData, data);
    strcat(postData, ",\"mnc\":");
    sprintf(data, "%i", tower->mnc);
    strcat(postData, data);
    strcat(postData, ",\"lac\":");
    sprintf(data, "%i", tower->lac);
    strcat(postData, data);
    strcat(postData, ",\"cid\":");
    sprintf(data, "%i", tower->cell_id);
    strcat(postData, data);
    strcat(postData, ",\"nmr\":[{\"bsic\": 0, \"bcch\": 0} ]}]}");
    char url[BUF_SIZE] = "https://positioning.hereapi.com/v2/locate?fallback=any&apiKey=";
    strcat(url, HERE_API_KEY);
    return do_geolocate(url, postData);
}

//location only for LTE so far.
location_result here_geolocate_lte(cell_tower * tower) {
    char postData[BUF_SIZE] = "{\"lte\": [";
    char hex[3] = {0, 0, 0};
    strcat(postData, "{\"mcc\":");
    char data[16] = {0};
    sprintf(data, "%i", tower->mcc);
    strcat(postData, data);
    strcat(postData, ",\"mnc\":");
    sprintf(data, "%i", tower->mnc);
    strcat(postData, data);
    strcat(postData, ",\"cid\":");
    sprintf(data, "%i", tower->cell_id);
    strcat(postData, data);
    strcat(postData, ",\"nmr\":[{\"earfcn\": 0, \"pci\": 0} ]}]}");
    char url[BUF_SIZE] = "https://positioning.hereapi.com/v2/locate?fallback=any&apiKey=";
    strcat(url, HERE_API_KEY);
    return do_geolocate(url, postData);
}

location_result here_geolocate_tower(cell_tower * tower) {
    location_result ret = {0, 0, 0, 0};

    if (tower->cell_id <= 65535 ) {
        ret = here_geolocate_gsm( tower);
    }

    if (!ret.valid ) {
        ret = here_geolocate_lte( tower);
    }

    return ret;
}

//  "radioType": "gsm",
location_result google_geolocate_lbs(bool lte, cell_tower * tower) {
    char postData[BUF_SIZE] = {0};
    strcat(postData, lte ? "{\"considerIp\": false,radioType:\"lte\",\"cellTowers\": [" : "{\"considerIp\": false,radioType:\"gsm\",\"cellTowers\": [");
    char hex[3] = {0, 0, 0};
    strcat(postData, "{\"mobileCountryCode\":");
    char data[16] = {0};
    sprintf(data, "%i", tower->mcc);
    strcat(postData, data);
    strcat(postData, ",\"mobileNetworkCode\":");
    sprintf(data, "%i", tower->mnc);
    strcat(postData, data);
    strcat(postData, ",\"locationAreaCode\":");
    sprintf(data, "%i", tower->lac);
    strcat(postData, data);
    strcat(postData, ",\"cellId\":");
    sprintf(data, "%i", tower->cell_id);
    strcat(postData, data);
    strcat(postData, "} ]}");
    char url[BUF_SIZE] = "https://www.googleapis.com/geolocation/v1/geolocate?key=";
    strcat(url, GOOGLE_API_KEY);
    return do_geolocate(url, postData);
}


location_result google_geolocate_tower(cell_tower * tower) {
    location_result ret = {0, 0, 0, 0};

    if (tower->cell_id <= 65535 ) {
        ret = google_geolocate_lbs(false, tower);
    }

    if (!ret.valid ) {
        ret = google_geolocate_lbs(true, tower);
    }

    return ret;
}

location_result geolocate_tower(cell_tower * tower) {
    location_result ret = google_geolocate_tower(tower);

    if (!ret.valid) {
        ret = here_geolocate_tower(tower);

        if (ret.radius > 10000) {
            ret.valid = false;
        }
    }

    return ret;
}

location_result geolocate_wifi(wifi_network * network, size_t network_count) {
    location_result ret = google_geolocate_wifi(network, network_count);

    if (!ret.valid) {
        ret = here_geolocate_wifi(network, network_count);

        if (ret.valid && ret.radius > 200) {
            ret.valid = false;
        }
    }

    return ret;
}
