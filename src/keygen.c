/*
 * Copyright (C) 2015 Mathias Brossard <mathias@brossard.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "common.h"
#include "crypto.h"
#include "keypair.h"
#include "pkcs11_display.h"

static const char *app_name = "pkcs11-util keygen";

static const struct option options[] = {
    { "help",               0, 0,           'h' },
    { "pin",                1, 0,           'p' },
    { "slot",               1, 0,           's' },
    { "module",             1, 0,           'm' },
    { "directory",          1, 0,           'd' },
    { "key-type",           1, 0,           'k' },
#ifdef HAVE_OPENSSL
    { "label",              1, 0,           'l' },
#endif
    { 0, 0, 0, 0 }
};

static const char *option_help[] = {
    "Print this help and exit",
    "Supply PIN on the command line (if used in scripts: careful!)",
    "Specify number of the slot to use",
    "Specify the module to load",
    "Specify the directory for NSS database",
    "Key type",
    "Label to set",
};

int keygen( int argc, char **argv )
{
    CK_ULONG          nslots;
    CK_SLOT_ID        *pslots = NULL;
    CK_FUNCTION_LIST  *funcs = NULL;
    CK_BYTE           opt_pin[20] = "";
    CK_BYTE_PTR       opt_label = NULL;
    CK_ULONG          opt_pin_len = 0;
    CK_RV             rc;
    CK_ULONG          opt_slot = -1;
    char *opt_module = NULL, *opt_dir = NULL;
    char *gen_param = NULL;
    int long_optind = 0;
    int genkey = 0;

    char c;

    init_crypto();

    while (1) {
        c = getopt_long(argc, argv, "ILMOd:hl:p:s:k:m:",
                        options, &long_optind);
        if (c == -1)
            break;
        switch (c) {
            case 'd':
                opt_dir = optarg;
                break;
            case 'l':
                opt_label = (CK_BYTE_PTR)optarg;
                break;
            case 'p':
                opt_pin_len = strlen(optarg);
                opt_pin_len = (opt_pin_len < 20) ? opt_pin_len : 19;
                memcpy( opt_pin, optarg, opt_pin_len );
                break;
            case 's':
                opt_slot = (CK_SLOT_ID) atoi(optarg);
                break;
            case 'm':
                opt_module = optarg;
                break;
            case 'k':
                gen_param = optarg;
                genkey = 1;
                break;
            case 'h':
            default:
                print_usage_and_die(app_name, options, option_help);
        }
    }

    if(!genkey) {
        print_usage_and_die(app_name, options, option_help);
    }

    funcs = pkcs11_get_function_list( opt_module );
    if (!funcs) {
        printf("Could not get function list.\n");
        if(!opt_module) {
            print_usage_and_die(app_name, options, option_help);
        }
        return -1;
    }

    if(opt_dir) {
        fprintf(stderr, "Using %s directory\n", opt_dir);
    }

    rc = pkcs11_initialize_nss(funcs, opt_dir);
    if (rc != CKR_OK) {
        show_error(stdout, "C_Initialize", rc );
        return rc;
    }

    rc = pkcs11_get_slots(funcs, stdout, &pslots, &nslots);
    if (rc != CKR_OK) {
        return rc;
    }
    
    if(opt_slot != -1) {
        /* TODO: Look in pslots */
        pslots = &opt_slot;
        nslots = 1;
    } else {
        if(opt_pin_len) {
            printf("No slot specified, the '--pin' parameter will be ignored\n");
        }
    }

    if(opt_slot == -1) {
        rc = funcs->C_GetSlotList(0, NULL_PTR, &nslots);
        if (rc != CKR_OK) {
            show_error(stdout, "C_GetSlotList", rc );
            return rc;
        }
        
        if(nslots == 1) {
            rc = funcs->C_GetSlotList(0, &opt_slot, &nslots);
            if (rc != CKR_OK) {
                    show_error(stdout, "C_GetSlotList", rc );
                    return rc;
            } else {
                printf("Using slot %ld\n", opt_slot);
            }
        }
    }
    
    if(opt_slot != -1) {
        char             *tmp;
        long              keysize;
        CK_SESSION_HANDLE h_session;
        CK_FLAGS          flags = CKF_SERIAL_SESSION | CKF_RW_SESSION;
        rc = funcs->C_OpenSession( opt_slot, flags, NULL, NULL, &h_session );
        if(opt_pin_len) {
            rc = funcs->C_Login( h_session, CKU_USER, opt_pin, opt_pin_len );
            if (rc != CKR_OK) {
                show_error(stdout, "C_Login", rc );
            }
        } else {
            CK_TOKEN_INFO  info;
            
            rc = funcs->C_GetTokenInfo( opt_slot, &info );
            if (rc != CKR_OK) {
                show_error(stdout, "C_GetTokenInfo", rc );
                return FALSE;
            }
            
            if(info.flags & CKF_PROTECTED_AUTHENTICATION_PATH) {
                rc = funcs->C_Login( h_session, CKU_USER, NULL, 0 );
                if (rc != CKR_OK) {
                    show_error(stdout, "C_Login", rc );
                    return rc;
                }
            }
        }
        fprintf(stdout, "Generating key with param '%s'\n", gen_param);
        keysize = strtol(gen_param, &tmp, 10);
        if(gen_param != tmp) {
            fprintf(stdout, "Generating RSA key with size %ld\n", keysize);
            rc = generateRsaKeyPair(funcs, h_session, keysize, opt_label);
        } else if(strncmp(gen_param, "gost", 4) == 0) {
            fprintf(stdout, "Generating GOST R34.10-2001 key (%s) in slot %ld\n",
                    gen_param, opt_slot);
            rc = generateGostKeyPair(funcs, h_session, gen_param, opt_label);
        } else {
            CK_BBOOL full;
                rc = ecdsaNeedsEcParams(funcs, opt_slot, &full);
                if(rc == CKR_OK) {
                    fprintf(stdout, "Generating ECDSA key with curve '%s' "
                            "in slot %ld with %s\n", gen_param, opt_slot,
                            full ? "EC Parameters" : "Named Curve");
                    rc = generateEcdsaKeyPair(funcs, h_session, gen_param, full, opt_label);
                }
        }
    } else {
        printf("The key generation function requires the '--slot' parameter\n");
    }

    rc = funcs->C_Finalize( NULL );
    return rc;
}