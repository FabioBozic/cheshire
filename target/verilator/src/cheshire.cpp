// Copyright 2022 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Modified version of the CVA6 testbench
// (https://github.com/openhwgroup/cva6,
// 99acdc271b90ce5abeb1b682eff4f999d0977ff4)
//
// Jannis Schönleber

#include "Vcheshire_testharness.h"
#include "verilated.h"
#include <verilated_vcd_c.h>
#if (VERILATOR_VERSION_INTEGER >= 5000000)
// Verilator v5 adds $root wrapper that provides rootp pointer.
#include "Vcheshire_testharness___024root.h"
#endif

#include <fesvr/dtm.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fesvr/elfloader.h>
#include <fesvr/htif_hexwriter.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <stdio.h>
#include <string>
#include <unistd.h>

// This software is heavily based on Rocket Chip
// Checkout this awesome project:
// https://github.com/freechipsproject/rocket-chip/

// This is a 64-bit integer to reduce wrap over issues and
// allow modulus.  You can also use a double, if you wish.
static vluint64_t main_time = 0;
int clk_ratio = 2;

unsigned int ir_select[5] = {0, 0, 0, 0, 1};
const unsigned int IDCode[32] = {1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 1};

static void cycle_start(std::shared_ptr<Vcheshire_testharness> top) {
    top->jtag_tck = 1;
    for (int i = 0; i < clk_ratio; i++) {
        top->clk_i = 1;
        if (main_time % 1500000 == 0) top->rtc_i ^= 1;
        //printf("tick\n");
        top->eval();
        main_time += 2500;
        top->clk_i = 0;
        if (main_time % 1500000 == 0) top->rtc_i ^= 1;
        top->eval();
        main_time += 2500;
    }
}

static void cycle_end(std::shared_ptr<Vcheshire_testharness> top) {
    top->jtag_tck = 0;
    for (int i = 0; i < clk_ratio; i++) {
        top->clk_i = 1;
        if (main_time % 1500000 == 0) top->rtc_i ^= 1;
        top->eval();
        main_time += 2500;
        top->clk_i = 0;
        if (main_time % 1500000 == 0) top->rtc_i ^= 1;
        top->eval();
        main_time += 2500;
    }
}

static void wait_cycles(std::shared_ptr<Vcheshire_testharness> top, int cycles) {
    for (int i = 0; i < cycles; i++) {
        cycle_start(top);
        cycle_end(top);
    }
}

static void reset_master (std::shared_ptr<Vcheshire_testharness> top){

  top->jtag_tms = 1;
  top->jtag_tdi = 0;
  top->jtag_trst_n = 0;

  wait_cycles(top, 2);
  top->jtag_trst_n = 1;
  for(int i = 0; i < 5; i++){
    ir_select[i] = 0;
  }
  ir_select[4] = 1;
  wait_cycles(top, 1);
  
}

static void soft_reset (std::shared_ptr<Vcheshire_testharness> top){
  top->jtag_tms = 1;
  top->jtag_tdi = 0;
  wait_cycles(top, 6);
  top->jtag_tms = 0;
  wait_cycles(top, 1);
  for(int i = 0; i < 5; i++){
    ir_select[i] = 0;
  }
  ir_select[4] = 1;
}

static void write_tms (std::shared_ptr<Vcheshire_testharness> top, int input) {
  top->jtag_tms = input;
  wait_cycles(top, 1);
}

static void write_bits(std::shared_ptr<Vcheshire_testharness> top, unsigned int* wdata, unsigned int tms_last, 
                      int size) {
  for(int i = 0; i < size; i++) {
    top->jtag_tdi = wdata[(size - 1) - i];
    if (i == (size - 1)) top->jtag_tms = tms_last;
    //printf("%d [write_bits] jtag_tdi: %d, jtag_tms: %d, wdata: %d\n", i, top->jtag_tdi, top->jtag_tms, wdata[i]);
    wait_cycles(top, 1);
  }
  //for (int i = 0; i < size; i++) printf("%d", wdata[i]);
  //printf("\n");
  top->jtag_tms = 0;

}

static void shift_dr(std::shared_ptr<Vcheshire_testharness> top){
  write_tms(top, 1);
  //printf("1 [shift_dr] jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("2 [shift_dr] jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("3 [shift_dr] jtag_tms: %d\n", top->jtag_tms);
}

static void update_dr (std::shared_ptr<Vcheshire_testharness> top, int exit_1_dr){
  if (exit_1_dr) write_tms(top, 1);
  //printf("1 [update_dr] jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 1);
  //printf("2 [update_dr] jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("3 [update_dr] jtag_tms: %d\n", top->jtag_tms);
}

static void readwrite_bits (std::shared_ptr<Vcheshire_testharness> top, unsigned int* rdata, unsigned int* wdata, 
                                    unsigned int tms_last, int size){
  
  
  for (int i = 0; i < size; i++){
    top->jtag_tdi = wdata[i];
    if (i == (size - 1)) top->jtag_tms = tms_last;
    
    cycle_start(top);
    rdata[(size - 1) - i] = top->jtag_tdo;
    //printf("%d [readwrite_bits] jtag_tdi: %d, jtag_tms: %d, jtag_tdo: %d, rdata: %d, wdata: %d\n", i, top->jtag_tdi, top->jtag_tms, top->jtag_tdo, rdata[(size - 1) - i], wdata[i]);
    cycle_end(top);

  }
  top->jtag_tms = 0;
  for (int i = 0; i < size; i++){
    //printf("%d", rdata[i]);
  }
  //printf("\n");
}

static void set_ir(std::shared_ptr<Vcheshire_testharness> top, unsigned int* opcode, int size) {
  if (ir_select[0] == opcode[0] && ir_select[1] == opcode[1] &&
      ir_select[2] == opcode[2] && ir_select[3] == opcode[3] &&
      ir_select[4] == opcode[4]) {
        //printf("\nir_select is the same\n");
        return;
      }
  write_tms(top, 1);
  //printf("test set_ir 1, jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 1);
  //printf("test set_ir 2, jtag_tms: %c\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("test set_ir 3, jtag_tms: %c\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("test set_ir 4, jtag_tms: %d\n", top->jtag_tms);
  write_bits(top, opcode, 1, size);
  //printf("test set_ir 5, jtag_tms: %d\n", top->jtag_tms);
  write_tms(top, 1);
  //printf("test set_ir 6, jtag_tms: %c\n", top->jtag_tms);
  write_tms(top, 0);
  //printf("test set_ir 7, jtag_tms: %c\n", top->jtag_tms);
  for (int i = 0; i < 5; i++){
    ir_select[i] = opcode[i];
  }
}

static void get_idcode(std::shared_ptr<Vcheshire_testharness> top, unsigned int* IDCODE, unsigned int* rdata_32){
  unsigned int wdata[32];
  int size = 32;
  for(int i = 0; i < 32; i++) wdata[i] = 0;
  set_ir(top, IDCODE, 5);
  shift_dr(top);
  readwrite_bits(top, rdata_32, wdata, 0, size);
  update_dr(top, 1);
}

static void write_dtmcs(std::shared_ptr<Vcheshire_testharness> top, unsigned int* data){
  unsigned int write_data[32];
  unsigned int DTMCSR[5] = {1, 0, 0, 0, 0};
  int size = 32;
  for (int i = 0; i < 32; i++){
    write_data[i] = data[i];

  } 

  //for (int i = 0; i < 5; i++) printf("%d", DTMCSR[i]);
  //printf("\n");
  set_ir(top, DTMCSR, 5);
  shift_dr(top);
  write_bits(top, write_data, 1, size);
  update_dr(top, 0);
}

static void read_dtmcs(std::shared_ptr<Vcheshire_testharness> top, unsigned int* data, unsigned int* rdata_32){
  unsigned int write_data[32];
  unsigned int DTMCSR[5] = {1, 0, 0, 0, 0};
  int size = 32;
  for (int i = 0; i < 32; i++){
    write_data[i] = 0;
    //printf("%d", write_data[i]);
  }

  //printf("\n");
  
  set_ir(top, DTMCSR, 5);
  shift_dr(top);
  readwrite_bits(top, rdata_32, write_data, 1, size);
  update_dr(top, 0);

  //printf("data: ");
  for (int i = 0; i < 32; i++){
    //printf("%d", data[i]);
  }
  //printf("\n");

 // return data;

}

static void write_dmi(std::shared_ptr<Vcheshire_testharness> top, unsigned int* address, unsigned int* input){
  unsigned int write_data[41];
  for(int i = 0; i < 7; i++){
    write_data[i] = address[i];
  }
  for(int i = 0; i < 32; i++){
    write_data[i+7] = input[i];
  }
  write_data[39] = 1;
  write_data[40] = 0;
  //printf("\nwrite_data: \n");
  for(int i = 0; i < 41; i++){
    //printf("%d", write_data[i]);
  }
  //printf("\n");
  unsigned int DMIACCESS[5] = {1, 0, 0, 0, 1};
  //printf("\n");

  for(int i = 0; i < 5; i++){
    //printf("%d", DMIACCESS[i]);
  }
  //printf("\n");
  set_ir(top, DMIACCESS, 5);
  //for(int i = 0; i < 5; i++) printf("%d", ir_select[i]);
  //printf("\n");
  shift_dr(top);
  write_bits(top, write_data, 1, 41);
  update_dr(top, 0);
  
}

static void read_dmi(std::shared_ptr<Vcheshire_testharness> top, unsigned int* address, int wait_idle, unsigned int* rdata_41){
  unsigned int write_data[41];
  unsigned int DMIACCESS[5] = {1, 0, 0, 0, 1};
  for (int i = 0; i < 7; i++) write_data[i] = address[i];
  for (int i = 0; i < 32; i++) write_data[i+7] = 0;
  write_data[39] = 0;
  write_data[40] = 1;

  set_ir(top, DMIACCESS, 5);
  shift_dr(top);
  write_bits(top, write_data, 1, 41);
  update_dr(top, 0);
  wait_cycles(top, wait_idle);
  write_data[40] = 0;
  shift_dr(top);
  readwrite_bits(top, rdata_41, write_data, 1, 41);
  update_dr(top, 0);
}

int main(int argc, char **argv) {

    unsigned int idcode_input[5];

    
    for (int i = 0; i < 5; i++) idcode_input[i] = 0;
    idcode_input[4] = 1;
    /*for (int i = 0; i < 32; i++) set_dmactive[i] = 0;
    set_dmactive[31] = 1;
    for (int i = 0; i < 32; i++) printf("%d", set_dmactive[i]);*/
    printf("\n");
    
    unsigned int got_idcode[32];
    unsigned int got_dmi[32];
    unsigned int got_sbcs[32];

    unsigned int Data0[7] = {0, 0, 0, 0, 1, 0, 0};
    unsigned int Data1[7] = {0, 0, 0, 0, 1, 0, 1};
    unsigned int DMStatus[7] = {0, 0, 1, 0, 0, 0, 1};
    unsigned int AbstractCS[7] = {0, 0, 1, 0, 1, 1, 0};
    unsigned int Command[7] = {0, 0, 1, 0, 1, 1, 1};
    unsigned int SBAddress0[7] = {0, 1, 1, 1, 0, 0, 1};
    unsigned int SBAddress1[7] = {0, 1, 1, 1, 0, 1, 0};
    unsigned int SBData0[7] = {0, 1, 1, 1, 1, 0, 0};
    unsigned int SBData1[7] = {0, 1, 1, 1, 1, 0, 1};
    int wait_idle = 10;
    unsigned int rdata_32[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned int rdata_41[41];
    unsigned int* dtmcs_read;

    char ** htif_argv = NULL;

    int htif_argc = 1 + argc - optind;
    htif_argv = (char **) malloc((htif_argc) * sizeof (char *));
    htif_argv[0] = argv[0];
    for (int i = 1; optind < argc;) htif_argv[i++] = argv[optind++];

    Verilated::commandArgs(argc, argv);
    std::shared_ptr<Vcheshire_testharness> top(new Vcheshire_testharness);

    //load_binary
    htif_hexwriter_t htif(0x0, 1, -1);
    memif_t memif(&htif);
    reg_t entry;
    //load_elf(htif_argv[1], &memif, &entry);

    size_t mem_size = 0xFFFFFF;
    //memif.read(0x80000000, mem_size, (void *)top->rootp->cheshire_testharness__DOT__i_cheshire__DOT__gen_llc__DOT__i_llc__DOT__i_axi_llc_top_raw__DOT__i_llc_ways__DOT__gen_data_ways__BRA__0__KET____DOT__i_data_way__DOT__i_data_sram__DOT__sram.data());
    //printf("%d", memif); 
    unsigned int binaryin[64];
    //for (int i = 0; i < 64; i++) binaryin[i] = memif[i];
    //for (int i = 0; i < 32; i++) printf("%d", set_dmactive[i]);
    
    // reset
    for (int i = 0; i < 10; i++) {
        top->rst_ni = 0;
        top->rtc_i = 0;
        wait_cycles(top, 5);
    }
    top->rst_ni = 1;
    wait_cycles(top, 2);
    reset_master(top);
    soft_reset(top);
    wait_cycles(top, 12);

    get_idcode(top, idcode_input, rdata_32);
    printf("\nIDCode: (should)\n");
    for(int i = 0; i < 32; i++){
        printf("%d", IDCode[i]);
        got_idcode[i] = rdata_32[i];
    }
    printf("\nIDCode: (is)\n");
    for (int i = 0; i < 32; i++){
        printf("%d", got_idcode[i]);
    }
    if (got_idcode[0] != IDCode[0] || got_idcode[8] != IDCode[8] || got_idcode[16] != IDCode[16] || got_idcode[24] != IDCode[24] || 
        got_idcode[1] != IDCode[1] || got_idcode[9] != IDCode[9] || got_idcode[17] != IDCode[17] || got_idcode[25] != IDCode[25] || 
        got_idcode[2] != IDCode[2] || got_idcode[10] != IDCode[10] || got_idcode[18] != IDCode[18] || got_idcode[26] != IDCode[26] || 
        got_idcode[3] != IDCode[3] || got_idcode[11] != IDCode[11] || got_idcode[19] != IDCode[19] || got_idcode[27] != IDCode[27] || 
        got_idcode[4] != IDCode[4] || got_idcode[12] != IDCode[12] || got_idcode[20] != IDCode[20] || got_idcode[28] != IDCode[28] || 
        got_idcode[5] != IDCode[5] || got_idcode[13] != IDCode[13] || got_idcode[21] != IDCode[21] || got_idcode[29] != IDCode[29] || 
        got_idcode[6] != IDCode[6] || got_idcode[14] != IDCode[14] || got_idcode[22] != IDCode[22] || got_idcode[30] != IDCode[30] || 
        got_idcode[7] != IDCode[7] || got_idcode[15] != IDCode[15] || got_idcode[23] != IDCode[23] || got_idcode[31] != IDCode[31]) {
          printf("\n[JTAG] IDCode Mismatch\n[IDCODE TEST FAILED]\n");
          return 0;
        }
    else printf("\nIDCode is correct\n[IDCODE TEST PASSED]\n");
    printf("\n");

    unsigned int DMControl[7] = {0, 0, 1, 0, 0, 0, 0};
    unsigned int set_dmactive[32];
    for (int i = 0; i < 32; i++) set_dmactive[i] = 0;
    set_dmactive[31] = 1;
    //for (int i = 0; i < 32; i++) printf("%d", set_dmactive[i]);
    write_dmi(top, DMControl, set_dmactive);
    //printf("\n");
    //for (int i = 0; i < 32; i++) printf("%d", set_dmactive[i]);
    do {
        read_dmi(top, DMControl, 10, rdata_41);
        for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
        printf("set_dmactive: (input)\n");
        for (int i = 0; i < 32; i++) printf("%d", set_dmactive[i]);
        printf("\ndmi_read: (set_dmactive)\n");
        for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
        if (got_dmi[0] != set_dmactive[0] || got_dmi[8] != set_dmactive[8] || got_dmi[16] != set_dmactive[16] || got_dmi[24] != set_dmactive[24] ||
            got_dmi[1] != set_dmactive[1] || got_dmi[9] != set_dmactive[9] || got_dmi[17] != set_dmactive[17] || got_dmi[25] != set_dmactive[25] ||
            got_dmi[2] != set_dmactive[2] || got_dmi[10] != set_dmactive[10] || got_dmi[18] != set_dmactive[18] || got_dmi[26] != set_dmactive[26] ||
            got_dmi[3] != set_dmactive[3] || got_dmi[11] != set_dmactive[11] || got_dmi[19] != set_dmactive[19] || got_dmi[27] != set_dmactive[27] ||
            got_dmi[4] != set_dmactive[4] || got_dmi[12] != set_dmactive[12] || got_dmi[20] != set_dmactive[20] || got_dmi[28] != set_dmactive[28] ||
            got_dmi[5] != set_dmactive[5] || got_dmi[13] != set_dmactive[13] || got_dmi[21] != set_dmactive[21] || got_dmi[29] != set_dmactive[29] ||
            got_dmi[6] != set_dmactive[6] || got_dmi[14] != set_dmactive[14] || got_dmi[22] != set_dmactive[22] || got_dmi[30] != set_dmactive[30] ||
            got_dmi[7] != set_dmactive[7] || got_dmi[15] != set_dmactive[15] || got_dmi[23] != set_dmactive[23] || got_dmi[31] != set_dmactive[31]) {
            printf("\nDMControl is not on dmactive");
            
        }
        else {
        printf("\nDMControl in on dmactive\n[DMCONTROL TEST PASSED]\n");
        break;
        }
    } while(true);
    printf("\n");
    
    unsigned int SBCS[7] = {0, 1, 1, 1, 0, 0, 0};
    unsigned int set_sbcs[32];
    for (int i = 0; i < 32; i++) set_sbcs[i] = 0;
    //set_sbcs[2] = 1;
    set_sbcs[11] = 1; 
    //set_sbcs[12] = 1;
    set_sbcs[13] = 1;
    set_sbcs[15] = 1;
    //set_sbcs[14] = 1;
    set_sbcs[16] = 1;
    //set_sbcs[17] = 1;
    //set_sbcs[20] = 1;
    //set_sbcs[31] = 1;
    //set_sbcs[30] = 1;
    //set_sbcs[29] = 1;
    //set_sbcs[28] = 1;
    //set_sbcs[27] = 1;
    wait_cycles(top, 5);
    write_dmi(top, SBCS, set_sbcs);
    wait_cycles(top, 5);
    //for (int i = 0; i < 7; i++) printf("%d", SBCS[i]);
    //printf("\n");
    //for (int i = 0; i < 32; i++) printf("%d", set_sbcs[i]);
    //printf("\n");

    do {
        read_dmi(top, SBCS, 10, rdata_41);
        wait_cycles(top, 5);
        for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
        for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
        printf("\ntest\n");
    } while (got_dmi[10] == 1);



    int counter = 0;
    unsigned int data_in[32];
    for (int i = 0; i < 32; i++) data_in[i] = 0;
    data_in[6] = 1;
    for (int i = 0; i < 32; i++) printf("%d", data_in[i]);
    printf("\n");
    write_dmi(top, SBCS, set_sbcs);
    wait_cycles(top, 5);
    write_dmi(top, SBAddress0, data_in);
    wait_cycles(top, 5);
    printf("\n");
    do {
        read_dmi(top, SBData0, 10, rdata_41);
        //for (int i = 0; i < 41; i++) printf("%d", rdata_41[i]);
        //printf("\n");
        wait_cycles(top, 5);
        for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
        printf("[%d] ", counter);
        for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
        printf("\n");  
        counter += 1;
    } while (counter < 9);

    set_sbcs[11] = 0;
    set_sbcs[15] = 0;

    write_dmi(top, SBCS, set_sbcs);
    wait_cycles(top, 5);
    read_dmi(top, SBData0, 10, rdata_41);
    wait_cycles(top, 5);
    for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
    printf("[%d] ", counter);
    for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
    printf("\n");  

    //run binary
    /*unsigned int dm_data[32] = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    unsigned int cmdentry[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1};
    unsigned int status[32];
    for (int i = 0; i < 32; i++) status[i] = 0;
    write_dmi(top, DMControl, dm_data);
    do {
      read_dmi(top, DMStatus, 10, rdata_41);
      for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
      for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
    } while(got_dmi[22] == 0);
    printf("\n[JTAG] writing start address to register\n");

    for (int i = 0; i < 32; i++) dm_data[i] = mem_size[i];
    write_dmi(top, Data1, dm_data);

    for (int i = 0; i < 32; i++) dm_data[i] = mem_size[i + 32];
    write_dmi(top, Data0, dm_data);

    write_dmi(top, Command, cmdentry);

    do {
      read_dmi(top, AbstractCS, 10, rdata_41);
      for (int i = 0; i < 32; i++) got_dmi[i] = rdata_41[i + 7];
      for (int i = 0; i < 32; i++) printf("%d", got_dmi[i]);
    } while(got_dmi[19] == 1);

    dm_data[0] = 0;
    dm_data[1] = 1;
    write_dmi(top, DMControl, dm_data);
    printf("\n[JTAG] Resuming hart 0 from 0x%x", mem_size);*/

    printf("\nfinal\n");
    top->final();

    return 0;
}