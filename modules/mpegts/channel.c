/*
 * Astra Module: MPEG-TS (MPTS Demux)
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Module Name:
 *      channel
 *
 * Module Options:
 *      upstream    - object, stream instance returned by module_instance:stream()
 *      name        - string, channel name
 *      pnr         - number, join PID related to the program number
 *      pid         - list, join PID in list
 *      sdt         - boolean, join SDT table
 *      eit         - boolean, join EIT table
 *      map         - list, map PID by stream type, item format: "type=pid"
 *                    type: video, audio, rus, eng... and other languages code
 *                     pid: number identifier in range 32-8190
 *      filter      - list, drop PID
 */

#include <astra.h>

typedef struct
{
    char type[6];
    uint16_t origin_pid;
    uint16_t custom_pid;
    bool is_set;
} map_item_t;

struct module_data_t
{
    MODULE_LUA_DATA();
    MODULE_STREAM_DATA();

    /* Options */
    struct
    {
        const char *name;
        int pnr;
        int set_pnr;
        bool sdt;
        bool eit;
    } config;

    /* */
    asc_list_t *map;
    uint16_t pid_map[MAX_PID];
    uint8_t custom_ts[TS_PACKET_SIZE];

    mpegts_psi_t *pat;
    mpegts_psi_t *cat;
    mpegts_psi_t *pmt;
    mpegts_psi_t *sdt;
    mpegts_psi_t *eit;

    mpegts_packet_type_t stream[MAX_PID];

    uint16_t tsid;
    mpegts_psi_t *custom_pat;
    mpegts_psi_t *custom_pmt;
    mpegts_psi_t *custom_sdt;

    /* */
    uint8_t sdt_original_section_id;
    uint8_t sdt_max_section_id;
    uint32_t *sdt_checksum_list;

    uint8_t eit_cc;
};

#define MSG(_msg) "[channel %s] " _msg, mod->config.name

static void stream_reload(module_data_t *mod)
{
    memset(mod->stream, 0, sizeof(mod->stream));

    for(int __i = 0; __i < MAX_PID; ++__i)
    {
        if(mod->__stream.pid_list[__i])
            module_stream_demux_leave_pid(mod, __i);
    }

    mod->stream[0] = MPEGTS_PACKET_PAT;
    mod->stream[1] = MPEGTS_PACKET_CAT;

    mod->pat->crc32 = 0;
    mod->cat->crc32 = 0;
    mod->pmt->crc32 = 0;

    module_stream_demux_join_pid(mod, 0x00);
    module_stream_demux_join_pid(mod, 0x01);

    if(mod->config.sdt)
    {
        mod->stream[0x11] = MPEGTS_PACKET_SDT;
        module_stream_demux_join_pid(mod, 0x11);
        if(mod->sdt_checksum_list)
        {
            free(mod->sdt_checksum_list);
            mod->sdt_checksum_list = NULL;
        }
    }

    if(mod->config.eit)
    {
        mod->stream[0x12] = MPEGTS_PACKET_EIT;
        module_stream_demux_join_pid(mod, 0x12);
    }

    if(mod->map)
    {
        asc_list_for(mod->map)
        {
            map_item_t *map_item = asc_list_data(mod->map);
            map_item->is_set = false;
        }
    }
}

/*
 * oooooooooo   o   ooooooooooo
 *  888    888 888  88  888  88
 *  888oooo88 8  88     888
 *  888      8oooo88    888
 * o888o   o88o  o888o o888o
 *
 */

static void on_pat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(mod->custom_pat, (ts_callback_t)__module_stream_send, &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PAT checksum error"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PAT changed. Reload stream info"));
        stream_reload(mod);
    }

    psi->crc32 = crc32;

    mod->tsid = PAT_GET_TSID(psi);

    const uint8_t *pointer;

    PAT_ITEMS_FOREACH(psi, pointer)
    {
        const uint16_t pnr = PAT_ITEM_GET_PNR(psi, pointer);
        if(!pnr)
            continue;

        if(!mod->config.pnr)
            mod->config.pnr = pnr;

        if(pnr == mod->config.pnr)
        {
            const uint16_t pid = PAT_ITEM_GET_PID(psi, pointer);
            module_stream_demux_join_pid(mod, pid);
            mod->stream[pid] = MPEGTS_PACKET_PMT;
            mod->pmt->pid = pid;
            mod->pmt->crc32 = 0;
            break;
        }
    }

    if(PAT_ITEMS_EOL(psi, pointer))
    {
        mod->custom_pat->buffer_size = 0;
        asc_log_error(MSG("PAT: stream with id %d is not found"), mod->config.pnr);
        return;
    }

    const uint8_t pat_version = PAT_GET_VERSION(mod->custom_pat) + 1;
    PAT_INIT(mod->custom_pat, mod->tsid, pat_version);
    memcpy(PAT_ITEMS_FIRST(mod->custom_pat), pointer, 4);

    mod->custom_pmt->pid = mod->pmt->pid;

    if(mod->config.set_pnr)
    {
        uint8_t *custom_pointer = PAT_ITEMS_FIRST(mod->custom_pat);
        PAT_ITEM_SET_PNR(mod->custom_pat, custom_pointer, mod->config.set_pnr);
    }

    if(mod->map)
    {
        asc_list_for(mod->map)
        {
            map_item_t *map_item = asc_list_data(mod->map);
            if(map_item->is_set)
                continue;

            if(   (map_item->origin_pid && map_item->origin_pid == mod->pmt->pid)
               || (!strcmp(map_item->type, "pmt")) )
            {
                map_item->is_set = true;
                mod->pid_map[mod->pmt->pid] = map_item->custom_pid;

                uint8_t *custom_pointer = PAT_ITEMS_FIRST(mod->custom_pat);
                PAT_ITEM_SET_PID(mod->custom_pat, custom_pointer, map_item->custom_pid);

                mod->custom_pmt->pid = map_item->custom_pid;
                break;
            }
        }
    }

    mod->custom_pat->buffer_size = 8 + 4 + CRC32_SIZE;
    PSI_SET_SIZE(mod->custom_pat);
    PSI_SET_CRC32(mod->custom_pat);

    mpegts_psi_demux(mod->custom_pat, (ts_callback_t)__module_stream_send, &mod->__stream);
}

/*
 *   oooooooo8     o   ooooooooooo
 * o888     88    888  88  888  88
 * 888           8  88     888
 * 888o     oo  8oooo88    888
 *  888oooo88 o88o  o888o o888o
 *
 */

static void on_cat(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
        return;

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("CAT checksum mismatch"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("CAT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    const uint8_t *desc_pointer;

    CAT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
            if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
            {
                mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                module_stream_demux_join_pid(mod, ca_pid);
            }
        }
    }
}

/*
 * oooooooooo oooo     oooo ooooooooooo
 *  888    888 8888o   888  88  888  88
 *  888oooo88  88 888o8 88      888
 *  888        88  888  88      888
 * o888o      o88o  8  o88o    o888o
 *
 */

static uint16_t map_custom_pid(module_data_t *mod, uint16_t pid, const char *type)
{
    asc_list_for(mod->map)
    {
        map_item_t *map_item = asc_list_data(mod->map);
        if(map_item->is_set)
            continue;

        if(   (map_item->origin_pid && map_item->origin_pid == pid)
           || (!strcmp(map_item->type, type)) )
        {
            map_item->is_set = true;
            mod->pid_map[pid] = map_item->custom_pid;

            return map_item->custom_pid;
        }
    }
    return 0;
}

static void on_pmt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x02)
        return;

    // check pnr
    const uint16_t pnr = PMT_GET_PNR(psi);
    if(pnr != mod->config.pnr)
        return;

    // check changes
    const uint32_t crc32 = PSI_GET_CRC32(psi);
    if(crc32 == psi->crc32)
    {
        mpegts_psi_demux(mod->custom_pmt, (ts_callback_t)__module_stream_send, &mod->__stream);
        return;
    }

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("PMT checksum error"));
        return;
    }

    // reload stream
    if(psi->crc32 != 0)
    {
        asc_log_warning(MSG("PMT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    psi->crc32 = crc32;

    const uint16_t pcr_pid = PMT_GET_PCR(psi);
    bool join_pcr = true;

    const uint8_t *desc_pointer;

    PMT_DESC_FOREACH(psi, desc_pointer)
    {
        if(desc_pointer[0] == 0x09)
        {
            const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
            if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
            {
                mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                module_stream_demux_join_pid(mod, ca_pid);
            }
        }
    }

    mod->custom_pmt->buffer_size = PMT_ITEMS_FIRST(psi) - psi->buffer;
    memcpy(mod->custom_pmt->buffer, psi->buffer, mod->custom_pmt->buffer_size);

    if(mod->config.set_pnr)
    {
        PMT_SET_PNR(mod->custom_pmt, mod->config.set_pnr);
    }

    const uint8_t *pointer;

    PMT_ITEMS_FOREACH(psi, pointer)
    {
        const uint16_t pid = PMT_ITEM_GET_PID(psi, pointer);

        if(mod->pid_map[pid] == MAX_PID)
        { // skip filtered pid
            continue;
        }

        const uint16_t item_size = 5 + __PMT_ITEM_DESC_SIZE(pointer);
        uint8_t *custom_pointer = &mod->custom_pmt->buffer[mod->custom_pmt->buffer_size];
        memcpy(custom_pointer, pointer, item_size);
        mod->custom_pmt->buffer_size += item_size;

        module_stream_demux_join_pid(mod, pid);

        if(pid == pcr_pid)
            join_pcr = false;

        PMT_ITEM_DESC_FOREACH(pointer, desc_pointer)
        {
            if(desc_pointer[0] == 0x09)
            {
                const uint16_t ca_pid = DESC_CA_PID(desc_pointer);
                if(mod->stream[ca_pid] == MPEGTS_PACKET_UNKNOWN && ca_pid != NULL_TS_PID)
                {
                    mod->stream[ca_pid] = MPEGTS_PACKET_CA;
                    module_stream_demux_join_pid(mod, ca_pid);
                }
            }
        }

        if(mod->map)
        {
            uint16_t custom_pid = 0;

            switch(mpegts_pes_type(PMT_ITEM_GET_TYPE(psi, pointer)))
            {
                case MPEGTS_PACKET_VIDEO:
                {
                    custom_pid = map_custom_pid(mod, pid, "video");
                    break;
                }
                case MPEGTS_PACKET_AUDIO:
                {
                    PMT_ITEM_DESC_FOREACH(pointer, desc_pointer)
                    {
                        if(desc_pointer[0] == 0x0A)
                        {
                            char type[4];
                            type[0] = desc_pointer[2];
                            type[1] = desc_pointer[3];
                            type[2] = desc_pointer[4];
                            type[3] = 0;
                            custom_pid = map_custom_pid(mod, pid, type);
                            break;
                        }
                    }
                    if(!custom_pid)
                        custom_pid = map_custom_pid(mod, pid, "audio");
                    break;
                }
                default:
                    break;
            }

            if(custom_pid)
                PMT_ITEM_SET_PID(mod->custom_pmt, custom_pointer, custom_pid);
        }
    }
    mod->custom_pmt->buffer_size += CRC32_SIZE;

    if(join_pcr)
        module_stream_demux_join_pid(mod, pcr_pid);

    if(mod->map)
    {
        if(mod->pid_map[pcr_pid])
            PMT_SET_PCR(mod->custom_pmt, mod->pid_map[pcr_pid]);
    }

    PSI_SET_SIZE(mod->custom_pmt);
    PSI_SET_CRC32(mod->custom_pmt);
    mpegts_psi_demux(mod->custom_pmt, (ts_callback_t)__module_stream_send, &mod->__stream);
}

/*
 *  oooooooo8 ooooooooo   ooooooooooo
 * 888         888    88o 88  888  88
 *  888oooooo  888    888     888
 *         888 888    888     888
 * o88oooo888 o888ooo88      o888o
 *
 */

static void on_sdt(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    if(psi->buffer[0] != 0x42)
        return;

    if(mod->tsid != SDT_GET_TSID(psi))
        return;

    const uint32_t crc32 = PSI_GET_CRC32(psi);

    // check crc
    if(crc32 != PSI_CALC_CRC32(psi))
    {
        asc_log_error(MSG("SDT checksum error"));
        return;
    }

    // check changes
    if(!mod->sdt_checksum_list)
    {
        const uint8_t max_section_id = SDT_GET_LAST_SECTION_NUMBER(psi);
        mod->sdt_max_section_id = max_section_id;
        mod->sdt_checksum_list = calloc(max_section_id + 1, sizeof(uint32_t));
    }
    const uint8_t section_id = SDT_GET_SECTION_NUMBER(psi);
    if(section_id > mod->sdt_max_section_id)
    {
        asc_log_warning(MSG("SDT: section_number is greater then section_last_number"));
        return;
    }
    if(mod->sdt_checksum_list[section_id] == crc32)
    {
        if(mod->sdt_original_section_id == section_id)
            mpegts_psi_demux(mod->custom_sdt, (ts_callback_t)__module_stream_send, &mod->__stream);

        return;
    }

    if(mod->sdt_checksum_list[section_id] != 0)
    {
        asc_log_warning(MSG("SDT changed. Reload stream info"));
        stream_reload(mod);
        return;
    }

    mod->sdt_checksum_list[section_id] = crc32;

    const uint8_t *pointer;
    SDT_ITEMS_FOREACH(psi, pointer)
    {
        if(SDT_ITEM_GET_SID(psi, pointer) == mod->config.pnr)
            break;
    }

    if(SDT_ITEMS_EOL(psi, pointer))
        return;

    mod->sdt_original_section_id = section_id;

    memcpy(mod->custom_sdt->buffer, psi->buffer, 11); // copy SDT header
    SDT_SET_SECTION_NUMBER(mod->custom_sdt, 0);
    SDT_SET_LAST_SECTION_NUMBER(mod->custom_sdt, 0);

    const uint16_t item_length = __SDT_ITEM_DESC_SIZE(pointer) + 5;
    memcpy(&mod->custom_sdt->buffer[11], pointer, item_length);
    const uint16_t section_length = item_length + 8 + CRC32_SIZE;
    mod->custom_sdt->buffer_size = 3 + section_length;
    PSI_SET_SIZE(mod->custom_sdt);
    PSI_SET_CRC32(mod->custom_sdt);

    mpegts_psi_demux(mod->custom_sdt, (ts_callback_t)__module_stream_send, &mod->__stream);
}

/*
 * ooooooooooo ooooo ooooooooooo
 *  888    88   888  88  888  88
 *  888ooo8     888      888
 *  888    oo   888      888
 * o888ooo8888 o888o    o888o
 *
 */

static void on_eit(void *arg, mpegts_psi_t *psi)
{
    module_data_t *mod = arg;

    const uint8_t table_id = psi->buffer[0];
    const bool is_actual_eit = (table_id == 0x4E || (table_id >= 0x50 && table_id <= 0x5F));
    if(!is_actual_eit)
        return;

    if(mod->tsid != EIT_GET_TSID(psi))
        return;

    if(mod->config.pnr != EIT_GET_PNR(psi))
        return;

    psi->cc = mod->eit_cc;

    if(mod->config.set_pnr)
        EIT_SET_PNR(psi, mod->config.set_pnr);

    mpegts_psi_demux(psi, (ts_callback_t)__module_stream_send, &mod->__stream);

    mod->eit_cc = psi->cc;
}

/*
 * ooooooooooo  oooooooo8
 * 88  888  88 888
 *     888      888oooooo
 *     888             888
 *    o888o    o88oooo888
 *
 */

static void on_ts(module_data_t *mod, const uint8_t *ts)
{
    const uint16_t pid = TS_PID(ts);
    if(!module_stream_demux_check_pid(mod, pid))
        return;

    if(pid == NULL_TS_PID)
        return;

    switch(mod->stream[pid])
    {
        case MPEGTS_PACKET_UNKNOWN:
        case MPEGTS_PACKET_CA:
            break;
        case MPEGTS_PACKET_PAT:
            mpegts_psi_mux(mod->pat, ts, on_pat, mod);
            return;
        case MPEGTS_PACKET_CAT:
            mpegts_psi_mux(mod->cat, ts, on_cat, mod);
            break;
        case MPEGTS_PACKET_PMT:
            mpegts_psi_mux(mod->pmt, ts, on_pmt, mod);
            return;
        case MPEGTS_PACKET_SDT:
            mpegts_psi_mux(mod->sdt, ts, on_sdt, mod);
            return;
        case MPEGTS_PACKET_EIT:
            mpegts_psi_mux(mod->eit, ts, on_eit, mod);
            return;
        default:
            break;
    }

    if(mod->pid_map[pid] == MAX_PID)
        return;

    if(mod->map)
    {
        const uint16_t custom_pid = mod->pid_map[pid];
        if(custom_pid)
        {
            memcpy(mod->custom_ts, ts, TS_PACKET_SIZE);
            TS_SET_PID(mod->custom_ts, custom_pid);
            module_stream_send(mod, mod->custom_ts);
            return;
        }
    }

    module_stream_send(mod, ts);
}

/*
 * oooo     oooo  ooooooo  ooooooooo  ooooo  oooo ooooo       ooooooooooo
 *  8888o   888 o888   888o 888    88o 888    88   888         888    88
 *  88 888o8 88 888     888 888    888 888    88   888         888ooo8
 *  88  888  88 888o   o888 888    888 888    88   888      o  888    oo
 * o88o  8  o88o  88ooo88  o888ooo88    888oo88   o888ooooo88 o888ooo8888
 *
 */

static void __parse_map_item(module_data_t *mod, const char *item)
{
    map_item_t *map_item = calloc(1, sizeof(map_item_t));
    uint8_t i = 0;
    for(; i < sizeof(map_item->type) && item[i] && item[i] != '='; ++i)
        map_item->type[i] = item[i];

    asc_assert(item[i] == '=', "option 'map' has wrong format");

    map_item->origin_pid = atoi(map_item->type);
    ++i; // skip '='
    map_item->custom_pid = atoi(&item[i]);

    asc_list_insert_tail(mod->map, map_item);
}

static void module_init(module_data_t *mod)
{
    module_stream_init(mod, on_ts);
    module_stream_demux_set(mod, NULL, NULL);

    module_option_string("name", &mod->config.name, NULL);
    asc_assert(mod->config.name != NULL, "[channel] option 'name' is required");

    if(module_option_number("pnr", &mod->config.pnr))
    {
        module_option_number("set_pnr", &mod->config.set_pnr);

        mod->pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
        mod->cat = mpegts_psi_init(MPEGTS_PACKET_CAT, 1);
        mod->pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
        mod->custom_pat = mpegts_psi_init(MPEGTS_PACKET_PAT, 0);
        mod->custom_pmt = mpegts_psi_init(MPEGTS_PACKET_PMT, MAX_PID);
        mod->stream[0] = MPEGTS_PACKET_PAT;
        module_stream_demux_join_pid(mod, 0);
        mod->stream[1] = MPEGTS_PACKET_CAT;
        module_stream_demux_join_pid(mod, 1);

        module_option_boolean("sdt", &mod->config.sdt);
        if(mod->config.sdt)
        {
            mod->sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
            mod->custom_sdt = mpegts_psi_init(MPEGTS_PACKET_SDT, 0x11);
            mod->stream[0x11] = MPEGTS_PACKET_SDT;
            module_stream_demux_join_pid(mod, 0x11);
        }

        module_option_boolean("eit", &mod->config.eit);
        if(mod->config.eit)
        {
            mod->eit = mpegts_psi_init(MPEGTS_PACKET_EIT, 0x12);
            mod->stream[0x12] = MPEGTS_PACKET_EIT;
            module_stream_demux_join_pid(mod, 0x12);
        }
    }
    else
    {
        lua_getfield(lua, MODULE_OPTIONS_IDX, "pid");
        if(lua_istable(lua, -1))
        {
            lua_foreach(lua, -2)
            {
                const int pid = lua_tonumber(lua, -1);
                module_stream_demux_join_pid(mod, pid);
            }
        }
        lua_pop(lua, 1); // pid
    }

    lua_getfield(lua, MODULE_OPTIONS_IDX, "map");
    if(lua_istable(lua, -1))
    {
        mod->map = asc_list_init();
        lua_foreach(lua, -2)
        {
            const char *map_item = lua_tostring(lua, -1);
            __parse_map_item(mod, map_item);
        }
    }
    lua_pop(lua, 1); // map

    lua_getfield(lua, MODULE_OPTIONS_IDX, "filter");
    if(lua_istable(lua, -1))
    {
        lua_foreach(lua, -2)
        {
            const int pid = lua_tonumber(lua, -1);
            mod->pid_map[pid] = MAX_PID;
        }
    }
    lua_pop(lua, 1); // filter
}

static void module_destroy(module_data_t *mod)
{
    module_stream_destroy(mod);

    mpegts_psi_destroy(mod->pat);
    mpegts_psi_destroy(mod->cat);
    mpegts_psi_destroy(mod->pmt);
    mpegts_psi_destroy(mod->custom_pat);
    mpegts_psi_destroy(mod->custom_pmt);

    if(mod->sdt)
    {
        mpegts_psi_destroy(mod->sdt);
        mpegts_psi_destroy(mod->custom_sdt);

        if(mod->sdt_checksum_list)
            free(mod->sdt_checksum_list);
    }

    if(mod->eit)
        mpegts_psi_destroy(mod->eit);

    if(mod->map)
    {
        for(asc_list_first(mod->map); !asc_list_eol(mod->map); asc_list_first(mod->map))
        {
            free(asc_list_data(mod->map));
            asc_list_remove_current(mod->map);
        }
        asc_list_destroy(mod->map);
    }
}

MODULE_STREAM_METHODS()
MODULE_LUA_METHODS()
{
    MODULE_STREAM_METHODS_REF()
};
MODULE_LUA_REGISTER(channel)
