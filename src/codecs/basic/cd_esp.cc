/*
** Copyright (C) 2002-2013 Sourcefire, Inc.
** Copyright (C) 1998-2002 Martin Roesch <roesch@sourcefire.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
// cd_esp.cc author Josh Rosenbaum <jorosenba@cisco.com>



#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "framework/codec.h"
#include "codecs/codec_events.h"
#include "snort.h"
#include "codecs/decode_module.h"
#include "managers/packet_manager.h"
#include <cstring>


namespace
{

class EspCodec : public Codec
{
public:
    EspCodec() : Codec("ESP"){};
    ~EspCodec(){};


    virtual bool decode(const uint8_t *raw_pkt, const uint32_t len, 
        Packet *, uint16_t &lyr_len, int &next_prot_id);
    
};


/* ESP constants */
const uint16_t ESP_PROT_ID = 50;
const uint32_t ESP_HEADER_LEN = 8;
const uint32_t ESP_AUTH_DATA_LEN = 12;
const uint32_t ESP_TRAILER_LEN = 2;

struct CdPegs{
    PegCount processed = 0;
    PegCount discards = 0;
};

std::vector<const char*> peg_names =
{
    "NameCodec_processed",
    "NameCodec_discards",
};


} // anonymous namespace

static THREAD_LOCAL CdPegs counts;
static CdPegs gcounts;



/*
 * Function: DecodeESP(const uint8_t *, uint32_t, Packet *)
 *
 * Purpose: Attempt to decode Encapsulated Security Payload.
 *          The contents are probably encrypted, but ESP is sometimes used
 *          with "null" encryption, solely for Authentication.
 *          This is more of a heuristic -- there is no ESP field that specifies
 *          the encryption type (or lack thereof).
 *
 * Arguments: pkt => ptr to the packet data
 *            len => length from here to the end of the packet
 *            p   => ptr to the Packet struct being filled out
 *
 * Returns: void function
 */
bool EspCodec::decode(const uint8_t *raw_pkt, const uint32_t len, 
    Packet *p, uint16_t &lyr_len, int &next_prot_id)
{
    const uint8_t *esp_payload;
    uint8_t pad_length;

    if (!ScESPDecoding())
        return false;
    

    /* The ESP header contains a crypto Initialization Vector (IV) and
       a sequence number. Skip these. */
    if (len < (ESP_HEADER_LEN + ESP_AUTH_DATA_LEN + ESP_TRAILER_LEN))
    {
        /* Truncated ESP traffic. Bail out here and inspect the rest as payload. */
        codec_events::decoder_event(p, DECODE_ESP_HEADER_TRUNC);
        p->data = raw_pkt;
        p->dsize = (uint16_t) len;
        return false;
    }
    esp_payload = raw_pkt + ESP_HEADER_LEN;

    /* The Authentication Data at the end of the packet is variable-length.
       RFC 2406 says that Encryption and Authentication algorithms MUST NOT
       both be NULL, so we assume NULL Encryption and some other Authentication.

       The mandatory algorithms for Authentication are HMAC-MD5-96 and
       HMAC-SHA-1-96, so we assume a 12-byte authentication data at the end. */
    lyr_len = (ESP_HEADER_LEN + ESP_AUTH_DATA_LEN + ESP_TRAILER_LEN);

    pad_length = *(esp_payload + len - lyr_len);
    next_prot_id = *(esp_payload + len + 1 - lyr_len);

    /* Adjust the packet length to account for the padding.
       If the padding length is too big, this is probably encrypted traffic. */
    if (pad_length < len)
    {
        lyr_len += (pad_length);
    }
    else
    {
        p->packet_flags |= PKT_TRUST;
        p->data = esp_payload;
        p->dsize = (u_short) len - lyr_len;
        next_prot_id = -1;
        return true;
    }



    // If we cant' decode the pakcer anymore, this is probably encrypted.
    // set the data pointers and pretend this is an ip datagram.
    if (!PacketManager::has_codec(next_prot_id))
    {
        p->packet_flags |= PKT_UNSURE_ENCAP;
    }
    else 
    {
        p->packet_flags |= PKT_TRUST;
        p->data = esp_payload;
        p->dsize = (u_short) len - lyr_len;
    }

    return true;
}

static void get_protocol_ids(std::vector<uint16_t>& v)
{
    v.push_back(ESP_PROT_ID);
}

static Codec* ctor()
{
    return new EspCodec();
}

static void dtor(Codec *cd)
{
    delete cd;
}

static void sum()
{
    sum_stats((PegCount*)&gcounts, (PegCount*)&counts, peg_names.size());
    memset(&counts, 0, sizeof(counts));
}

static void stats(std::vector<PegCount> g_peg_counts, std::vector<const char*> g_peg_names)
{
    std::memcpy(&g_peg_counts, &counts, sizeof(CdPegs));
    g_peg_names.insert(g_peg_names.end(), peg_names.begin(), peg_names.end());
}



static const char* name = "esp_codec";

static const CodecApi esp_api =
{
    { PT_CODEC, name, CDAPI_PLUGIN_V0, 0, nullptr, nullptr },
    NULL, // pinit
    NULL, // pterm
    NULL, // tinit
    NULL, // tterm
    ctor, // ctor
    dtor, // dtor
    NULL, // get_dlt()
    get_protocol_ids,
    sum, // sum
    stats  // stats
};


const BaseApi* cd_esp = &esp_api.base;

