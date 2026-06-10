global-incdirs-y += include
global-incdirs-y += third_party/mldsa87_ref
global-incdirs-y += third_party/mldsa87_ref/mldsa/src
global-incdirs-y += third_party/mldsa87_ref/integration/optee
global-incdirs-y += third_party/keccak_plain64

srcs-y += ta_hesia.c
srcs-y += ta_hesia_mldsa_backend.c
srcs-y += third_party/mldsa87_ref/integration/optee/fips202.c
srcs-y += third_party/keccak_plain64/KeccakP-1600-opt64.c
srcs-y += third_party/mldsa87_ref/mldsa/src/ct.c
srcs-y += third_party/mldsa87_ref/mldsa/src/debug.c
srcs-y += third_party/mldsa87_ref/mldsa/src/packing.c
srcs-y += third_party/mldsa87_ref/mldsa/src/poly.c
srcs-y += third_party/mldsa87_ref/mldsa/src/poly_kl.c
srcs-y += third_party/mldsa87_ref/mldsa/src/polyvec.c
srcs-y += third_party/mldsa87_ref/mldsa/src/sign.c

cflags-y += -DMLD_CONFIG_FILE=\"../../integration/optee/config_ta.h\"
