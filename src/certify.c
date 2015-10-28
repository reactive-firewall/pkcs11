/*
 * Copyright (C) 2015 Mathias Brossard <mathias@brossard.org>
 */

#include "pkcs11-util.h"

#ifdef HAVE_OPENSSL

#include "common.h"
#include "crypto.h"
#include "pkcs11_display.h"

#include <string.h>
#include <getopt.h>
#include <stdio.h>

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/ecdsa.h>

static char *app_name = "pkcs11-util certify";

static const struct option options[] = {
    { "help",               0, 0,           'h' },
    { "pin",                1, 0,           'p' },
    { "slot",               1, 0,           's' },
    { "module",             1, 0,           'm' },
    { "label",              1, 0,           'l' },
    { "directory",          1, 0,           'd' },
    { "quiet",              0, 0,           'q' },
    { 0, 0, 0, 0 }
};

static const char *option_help[] = {
    "Print this help and exit",
    "Supply PIN on the command line",
    "Specify number of the slot to use",
    "Specify the module to load",
    "Specify label of the private key to use",
    "Specify the directory for NSS database",
    "Reduce amount of information printed",
};

int certify( int argc, char **argv )
{
    CK_FUNCTION_LIST *funcs = NULL;
    CK_UTF8CHAR_PTR   opt_pin = NULL;
    char             *opt_label = NULL;
    CK_ULONG          opt_pin_len = 0;
    CK_RV             rc;
    CK_ULONG          opt_slot = -1;
    CK_SESSION_HANDLE h_session;
    CK_OBJECT_HANDLE  key = -1;
    char *opt_module = NULL;
    int long_optind = 0;
    int opt_quiet = 0;
    char c;
    CK_OBJECT_CLASS class = CKO_PRIVATE_KEY;
    CK_ATTRIBUTE search[2];
    CK_ULONG count = 1;
    char *nss_dir = NULL;

    printf("This feature is a work in progress.\n");

    while (1) {
        c = getopt_long(argc, argv, "hd:ep:s:g:l:m:t:o:q",
                        options, &long_optind);
        if (c == -1)
            break;
        switch (c) {
            case 'd':
                nss_dir = optarg;
            case 'p':
                opt_pin = (CK_UTF8CHAR_PTR) strdup(optarg);
                if(opt_pin) {
                    opt_pin_len = strlen(optarg);
                }
                break;
            case 's':
                opt_slot = (CK_SLOT_ID) atoi(optarg);
                break;
            case 'm':
                opt_module = optarg;
                break;
            case 'l':
                opt_label = optarg;
                break;
            case 'q':
                opt_quiet = 1;
                break;
            case 'h':
            default:
                print_usage_and_die(app_name, options, option_help);
        }
    }

    funcs = pkcs11_get_function_list( opt_module );
    if (!funcs) {
        printf("Could not get function list.\n");
        return -1;
    }

    if(nss_dir) {
        rc = pkcs11_initialize_nss(funcs, nss_dir);
    } else {
        rc = pkcs11_initialize(funcs);
    }
    if (rc != CKR_OK) {
        show_error(stdout, "C_Initialize", rc );
        return rc;
    }

    rc = pkcs11_login_session(funcs, stdout, opt_slot, &h_session,
                              CK_FALSE, CKU_USER, opt_pin, opt_pin_len);
    if (rc != CKR_OK) {
        return rc;
    }

    fillAttribute(&(search[0]), CKA_CLASS, &class, sizeof(class));
    if(opt_label) {
        fillAttribute(&(search[1]), CKA_LABEL, opt_label, strlen(opt_label));
        count = 2;
    }

    rc = pkcs11_find_object(funcs, stdout, h_session, search,
                            count, &key, 1, &count);
    if (rc != CKR_OK) {
        return rc;
    }

    if(count == 0) {
        printf("No object found\n");
        exit(-1);
    }

    if(!opt_quiet) {
        print_object_info(funcs, stdout, 0, h_session, key);
    }

    EVP_PKEY *k = load_pkcs11_key(funcs, h_session, key);

    if(k == NULL) {
        printf("Error loading key\n");
        exit(-1);
    }

    X509 *crt = X509_new();
    X509_set_version(crt, 0);
    ASN1_INTEGER_set(X509_get_serialNumber(crt), 0);
    X509_gmtime_adj(X509_get_notBefore(crt), 0);
    X509_gmtime_adj(X509_get_notAfter(crt), (long)60 * 60 * 24 * 365);
    X509_set_pubkey(crt, k);
    X509_sign(crt, k, EVP_sha256());

    if(!opt_quiet) {
        X509_print_fp(stdout, crt);
    }
    PEM_write_X509(stdout, crt);

    if(*opt_pin != '\0') {
        rc = funcs->C_Logout(h_session);
        if (rc != CKR_OK) {
            show_error(stdout, "C_Logout", rc );
            return rc;
        }
    }

    rc = funcs->C_CloseSession(h_session);
    if (rc != CKR_OK) {
        show_error(stdout, "C_CloseSession", rc );
        return rc;
    }

    rc = funcs->C_Finalize(NULL);
    if (rc != CKR_OK) {
        show_error(stdout, "C_Finalize", rc );
        return rc;
    }

    return rc;
}
#endif
