#ifndef TA_HESIA_H
#define TA_HESIA_H

#define TA_HESIA_UUID \
    { 0xa17de805, 0x9dc1, 0x43ef, \
      { 0x93, 0x2b, 0x91, 0xf1, 0x07, 0xca, 0xd5, 0x7b } }

#define TA_HESIA_CMD_SEAL         0x0001
#define TA_HESIA_CMD_UNSEAL       0x0002
#define TA_HESIA_CMD_ROTATE_KEY   0x0003
#define TA_HESIA_CMD_WIPE_KEY     0x0004
#define TA_HESIA_CMD_HKDF         0x0005
#define TA_HESIA_CMD_CHECK_VERSION 0x0006
#define TA_HESIA_CMD_RESET_VERSION 0x0007
#define TA_HESIA_CMD_READ_VERSION  0x0008

#endif /* TA_HESIA_H */
