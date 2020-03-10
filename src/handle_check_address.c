#include "handle_check_address.h"
#include "os.h"
#include "btchip_helpers.h"
#include "bip32_path.h"
#include "btchip_ecc.h"
#include "btchip_apdu_get_wallet_public_key.h"
#include "cashaddr.h"
#include "segwit_addr.h"

bool derive_public_key(const bip32_path_t* path, cx_ecfp_public_key_t* public_key) {
    unsigned char privateComponent[32];
    cx_ecfp_private_key_t privKey;
    os_perso_derive_node_bip32(CX_CURVE_256K1, path->path, path->length,
                               privateComponent, NULL);
    cx_ecdsa_init_private_key(BTCHIP_CURVE, privateComponent, 32, &privKey);
    cx_ecfp_generate_pair(BTCHIP_CURVE, public_key, &privKey, 1);
    return true;
}

bool derive_compressed_public_key(const bip32_path_t* path, unsigned char* public_key, unsigned char public_key_length) {
    cx_ecfp_public_key_t pubKey;
    if (!derive_public_key(path, &pubKey))
        return false;
    btchip_compress_public_key_value(pubKey.W);
    os_memcpy(public_key, pubKey.W, 33);
    return true;
}

bool get_address_from_compressed_public_key(
    unsigned char format,
    unsigned char* compressed_pub_key,
    unsigned char payToAddressVersion,
    unsigned char payToScriptHashVersion,
    const char* native_segwit_prefix,
    char * address,
    unsigned char max_address_length
) {
    bool segwit = (format == P2_SEGWIT);
    bool nativeSegwit = (format == P2_NATIVE_SEGWIT);
    bool cashAddr = (format == P2_CASHADDR);
    int address_length;
    if (cashAddr) {
        uint8_t tmp[20];
        btchip_public_key_hash160(compressed_pub_key,   // IN
                                  33,                   // INLEN
                                  tmp);
        if (!cashaddr_encode(tmp, 20, address, max_address_length, CASHADDR_P2PKH))
            return false;
    } else if (!(segwit || nativeSegwit)) {
        // btchip_public_key_to_encoded_base58 doesn't add terminating 0,
        // so we will do this ourself
        address_length = btchip_public_key_to_encoded_base58(
            compressed_pub_key,     // IN
            33,                     // INLEN
            address,                // OUT
            max_address_length - 1, // MAXOUTLEN
            payToAddressVersion, 0);
        address[address_length] = 0;
    } else {
        uint8_t tmp[22];
        tmp[0] = 0x00;
        tmp[1] = 0x14;
        btchip_public_key_hash160(compressed_pub_key,   // IN
                                  33,                   // INLEN
                                  tmp + 2               // OUT
                                  );
        if (!nativeSegwit) {
            address_length = btchip_public_key_to_encoded_base58(
                tmp,                   // IN
                22,                    // INLEN
                address,               // OUT
                150,                   // MAXOUTLEN
                payToScriptHashVersion, 0);
            address[address_length] = 0;
        } else {
            if (!native_segwit_prefix)
                return false;
            if (!segwit_addr_encode(
                address,
                native_segwit_prefix, 0, tmp + 2, 20)) {
                return false;
            }
        }
    }
    return true;
}

bool derive_compressed_public_key_from_serialized_path(
    unsigned char* serialized_path,
    unsigned char serialized_path_length,
    unsigned char* compressed_public_key,
    unsigned char compressed_public_key_length) {
    bip32_path_t path;
    if (!parse_serialized_path(&path, serialized_path, serialized_path_length)) {
        PRINTF("Can't parse path\n");
        return false;
    }
    if (!derive_compressed_public_key(&path, compressed_public_key, compressed_public_key_length)) {
        PRINTF("Can't derive public key on given address\n");
        return false;
    }
    return true;
}

void handle_check_address(check_address_parameters_t* params, btchip_altcoin_config_t* coin_config) {
    unsigned char compressed_public_key[33];
    PRINTF("Params on the address %d\n",(unsigned int)params);
    PRINTF("Address to check %s\n",params->address_to_check);
    PRINTF("Insied handle_check_address\n");
    params->result = 0;
    if (params->address_to_check == 0) {
        PRINTF("Address to check == 0\n");
        return;
    }
    if (!derive_compressed_public_key_from_serialized_path(
        params->address_parameters + 1,
        params->address_parameters_length - 1,
        compressed_public_key,
        sizeof(compressed_public_key))) {
        return;
    }
    
    char address[51];
    if (!get_address_from_compressed_public_key(
        params->address_parameters[0],
        compressed_public_key,
        coin_config->p2pkh_version,
        coin_config->p2sh_version,
        coin_config->native_segwit_prefix,
        address,
        sizeof(address))) {
        PRINTF("Can't create address from given public key\n");
        return;
    }
    if ((strlen(address) != strlen(params->address_to_check)) ||
        os_memcmp(address, params->address_to_check, strlen(address)) != 0) {
        PRINTF("Addresses doesn't match\n");
        return;
    }
    PRINTF("Addresses  match\n");
    params->result = 1;
}