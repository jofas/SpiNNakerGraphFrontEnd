/***** slave.c/slave_summary
*
* COPYRIGHT
*  Copyright (c) The University of Manchester, 2011. All rights reserved.
*  SpiNNaker Project
*  Advanced Processor Technologies Group
*  School of Computer Science
*  Author: Arthur Ceccotti
*******/

#include "spin1_api.h"
#include <debug.h>
#include <simulation.h>
#include <circular_buffer.h>
#include <data_specification.h>

#include "common-typedefs.h"
#include "../db-typedefs.h"
#include "../memory_utils.h"
#include "../sdp_utils.h"
#include "pull.h"
#include "put.h"

#include "scan.h"

#include "../double_linked_list.h"
#include "../message_queue.h"

#define TIMER_PERIOD 100

//Globals
uint32_t time = 0;

static circular_buffer sdp_buffer;

uchar chipx;
uchar chipy;
uchar core;
uchar branch;

uint32_t myId;

Table* tables;

sdp_msg_t msg;

void update(uint ticks, uint b){
    use(ticks);
    use(b);

    time += TIMER_PERIOD;

    /*
    if(ticks == 10){
         // Get pointer to 1st virtual processor info struct in SRAM
        vcpu_t *sark_virtual_processor_info = (vcpu_t*) SV_VCPU;
        address_t address = (address_t) sark_virtual_processor_info[ROOT_CORE].user0;
        address_t root_data_address = data_specification_get_region(DB_DATA_REGION, address);
        table = (Table*)root_data_address;

        print_table(table);
    }
    */
}


uint32_t entriesInQueue = 0;
uchar* entryQueue;//TODO 2-D array. Todo HARDCODED

void sdp_packet_callback(uint mailbox, uint port) {
    use(port);

    sdp_msg_t* msg     = (sdp_msg_t*)mailbox;
    sdp_msg_t* msg_cpy = (sdp_msg_t*)sark_alloc(1, sizeof(sdp_msg_t) + sizeof(insertEntryQuery)); //msg->length);

    sark_word_cpy(msg_cpy, msg, sizeof(sdp_msg_t) + sizeof(insertEntryQuery));

    print_msg(msg_cpy);

    spin1_msg_free(msg);

    if (circular_buffer_add(sdp_buffer, msg_cpy)){
        if(!spin1_trigger_user_event(0, 0)){
          log_error("Unable to trigger user event.");
          //sark_delay_us(1);
        }
    }
    else{
        log_error("Unable to add msg_cpy to SDP circular buffer");
    }
}

address_t* addr;

uint32_t*  table_rows_in_this_core;
address_t* table_base_addr;

uchar getBranch(){
  switch(core){
    case 5:
    case 6:
    case 7:
    case 8:
      return 2;
    case 9:
    case 10:
    case 11:
    case 12:
      return 3;
    case 13:
    case 14:
    case 15:
    case 16:
      return 4;
    default:
      return -1;
  }
}

sdp_msg_t* send_empty_response_to_host(spiDBcommand cmd, id_t id){
    sdp_msg_t* msg = create_sdp_header_to_host();

    Response* r = (Response*)&msg->cmd_rc;
    r->id  = id;
    r->cmd = cmd;
    r->success = true;
    r->x = chipx;
    r->y = chipy;
    r->p = core;

    msg->length = sizeof(sdp_hdr_t) + sizeof(Response);

    if(!spin1_send_sdp_msg(msg, SDP_TIMEOUT)){
        log_error("Failed to send response to host");
        return NULL;
    }

    return msg;
}

void process_requests(uint arg0, uint arg1){

    uint32_t mailbox;
    while(circular_buffer_get_next(sdp_buffer, &mailbox)){
        sdp_msg_t* msg = (sdp_msg_t*)mailbox;

        spiDBQueryHeader* header = (spiDBQueryHeader*) &msg->cmd_rc;

        if(header->cmd == SELECT_RESPONSE){
            //gather responses

            selectResponse* selResp = (selectResponse*)header;
            log_info("Received selectResponse on table '%s' with addr %08x",
                     selResp->table->name, selResp->addr);
            breakInBlocks(selResp);

            continue;
        }

        #ifdef DB_TYPE_KEY_VALUE_STORE
            uint32_t info;
            uchar* k,v;

            switch(header->cmd){
                case PUT:;
                    log_info("PUT");
                    putQuery* putQ = (putQuery*) header;
                    log_info("  on address: %04x, k_v: %s", *addr, putQ->k_v);
                    info    = putQ->info;
                    k       = putQ->k_v;
                    v       = &putQ->k_v[k_size_from_info(info)];

                    put(addr, info, k, v);

                    send_empty_response_to_host(PUT, putQ->id);
                    break;
                case PULL:;
                    log_info("PULL");
                    pullQuery* pullQ = (pullQuery*) header;

                    info    = pullQ->info;
                    k       = pullQ->k;

                    value_entry* value_entry_ptr = pull(data_region, info, k);

                    if(value_entry_ptr){
                        log_info("Found: %s", value_entry_ptr->data);
                    }
                    else{
                        log_info("Not found...");
                    }
                    break;
                default:;
                    break;
            }
        #endif
        #ifdef DB_TYPE_RELATIONAL
            switch(header->cmd){
                case INSERT_INTO:;
                    log_info("INSERT_INTO");

                    insertEntryQuery* insertE = (insertEntryQuery*) header;

                    uint32_t table_index = getTableIndex(tables,
                                                         insertE->table_name);

                    if(table_index == -1){
                        log_error("Unable to find table of name '%s' in tables");
                        return;
                    }

                    Table* t = &tables[table_index];

                    Entry e = insertE->e;
                    //printEntry(&e);

                    log_info(" %s < (%s,%s)",
                             insertE->table_name, e.col_name, e.value);

                    uint32_t i = get_col_index(tables, e.col_name);
                    uint32_t p = get_byte_pos(tables, i);

                    //todo double check that it is in fact empty (NULL)
                    memcpy(&entryQueue[p], e.value, e.size);

                    if(++entriesInQueue == t->n_cols){ //TODO

                        address_t address_to_write =
                            data_region +
                            (uint32_t)table_base_addr[table_index] +
                            ((t->row_size
                               * table_rows_in_this_core[table_index]) + 3) / 4;

                        log_info("Flushing to address %08x (base is: %08x)",
                                 address_to_write,table_base_addr[table_index]);

                        table_rows_in_this_core[table_index]++;
                        t->current_n_rows++;

                        entriesInQueue = 0; //reset and null all of them

                        memcpy(address_to_write,entryQueue,t->row_size);

                        for(uint32_t i = 0; i < t->row_size; i++){
                            //log_info("entryQueue[%d] = %c (%02x)", i, entryQueue[i], entryQueue[i]);
                            entryQueue[i] = 0;
                        }

                        send_empty_response_to_host(INSERT_INTO, insertE->id);
                    }
                    break;
                default:;
                    log_info("[Warning] cmd not recognized: %d with id %d",
                             header->cmd, header->id);
                    break;
            }
        #endif

        // free the message to stop overload
        //spin1_msg_free(msg);
    }
}

void receive_data (uint key, uint payload)
{
    log_info("Received MC packet with key=%d, payload=%08x", key, payload);

    selectQuery* selQ = (selectQuery*) payload;

    if(selQ->cmd != SELECT){
        log_error("Unexpected MC packet with selQ->cmd == %d", selQ->cmd);
        return;
    }

    log_info("SELECT");

    uint32_t table_index = getTableIndex(tables, selQ->table_name);
    if(table_index == -1){
        log_error("  Unable to find table '%d'", selQ->table_name);
        return;
    }

    scan_ids(&tables[table_index],
             data_region + (uint32_t)table_base_addr[table_index],
             selQ,
             table_rows_in_this_core[table_index]);
}

void receive_data_void (uint key, uint unknown){
    use(key);
    use(unknown);
    log_error("Received unexpected MC packet with no payload.");
}

void c_main()
{
    chipx = spin1_get_chip_id() & 0xF0 >> 8;
    chipy = spin1_get_chip_id() & 0x0F;
    core  = spin1_get_core_id();
    branch = getBranch();

    myId  = chipx << 16 | chipy << 8 | core;

    log_info("Initializing Leaf (%d,%d,%d)\n", chipx, chipy, core);

    table_rows_in_this_core = (uint32_t*)sark_alloc(DEFAULT_NUMBER_OF_TABLES,
                                                    sizeof(uint32_t));
    table_base_addr = (address_t*)sark_alloc(DEFAULT_NUMBER_OF_TABLES,
                                             sizeof(address_t));

    address_t b = 0;
    for(uint i=0; i<DEFAULT_NUMBER_OF_TABLES; i++){
        table_rows_in_this_core[i] = 0;
        table_base_addr[i] = b;
        log_info("table_base_addr[%d] = %08x", i, table_base_addr[i]);

        b += DEFAULT_TABLE_SIZE_WORDS;
    }

    //entryQueue = (uchar*)sark_alloc(table->row_size,sizeof(uchar)); //TODO should prob. be somewhere else
    entryQueue = (uchar*)sark_alloc(1024,sizeof(uchar)); //TODO should prob. be somewhere else
    for(uint32_t i = 0; i < 1024; i++){
        entryQueue[i] = 0;
    }

    if (!initialize()) {
        rt_error(RTE_SWERR);
    }

    clear(data_region, CORE_DATABASE_SIZE_WORDS);

    addr = (address_t*)malloc(sizeof(address_t));
    *addr = data_region;

    tables = (Table*) 0x64502168; //TODO

                                  //todo not hardcoded
    //entryQueue = (Entry*)sark_alloc(4, sizeof(Entry));

    //recent_messages_queue   = init_double_linked_list();
    //unacknowledged_replies  = init_double_linked_list();

    spin1_set_timer_tick(TIMER_PERIOD);

    sdp_buffer = circular_buffer_initialize(150);

    // register callbacks
    spin1_callback_on(SDP_PACKET_RX,        sdp_packet_callback, 0);
    spin1_callback_on(USER_EVENT,           process_requests,    2);
    spin1_callback_on(TIMER_TICK,           update,              2);

    spin1_callback_on(MCPL_PACKET_RECEIVED, receive_data,        0);
    spin1_callback_on(MC_PACKET_RECEIVED,   receive_data_void,   0);

    simulation_run();
    //spin1_start (SYNC_NOWAIT);
}