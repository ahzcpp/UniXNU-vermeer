// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */


/* Host fixture; complete modified production attach function, stubbed callees. */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t u8;typedef uint32_t u32;
#define BIT(x) (1U<<(x))
#define GENMASK(h,l) ((0xffffffffU>>(31-(h)))&(~0U<<(l)))
#define u32_encode_bits(v,m) (((uint32_t)(v)<<__builtin_ctz((uint32_t)(m)))&(m))
#define DP_INVALID_BANK_ID -1
struct ath12k_dp_link_vif {u32 tcl_metadata;int vdev_id_check_en;u8 bank_id;};
struct ath12k_dp_vif {struct ath12k_dp_link_vif links[16];};
struct ath12k_vif {struct ath12k_dp_vif dp_vif;};
struct ath12k_base {int unused;};struct pdev {unsigned pdev_id;};
struct ath12k {struct ath12k_base *ab;struct pdev *pdev;};
struct ath12k_link_vif {struct ath12k_vif *ahvif;u8 link_id;unsigned vdev_id;};
static unsigned updates,bank_calls,errors;static int bank_result;static u32 bank_metadata;
static struct ath12k_dp_link_vif *ath12k_dp_vif_to_dp_link_vif(struct ath12k_dp_vif *v,u8 l){if(l>=16)abort();return &v->links[l];}
static void *ath12k_ab_to_dp(struct ath12k_base *b){return b;}
static void ath12k_dp_update_vdev_search(struct ath12k_link_vif *v){(void)v;updates++;}
static int ath12k_dp_tx_get_bank_profile(struct ath12k_base *b,struct ath12k_link_vif *v,void *d){(void)b;(void)d;bank_calls++;bank_metadata=v->ahvif->dp_vif.links[v->link_id].tcl_metadata;return bank_result;}
static void ath12k_err(struct ath12k_base *b,const char *s){(void)b;(void)s;errors++;}

#define HTT_TCL_META_DATA_TYPE			GENMASK(1, 0)
#define HTT_TCL_META_DATA_VALID_HTT		BIT(2)
#define HTT_TCL_META_DATA_VDEV_ID		 GENMASK(10, 3)
#define HTT_TCL_META_DATA_PDEV_ID		 GENMASK(12, 11)
void ath12k_dp_vdev_tx_attach(struct ath12k *ar, struct ath12k_link_vif *arvif)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_vif *ahvif = arvif->ahvif;
	u8 link_id = arvif->link_id;
	int bank_id;
	struct ath12k_dp_link_vif *dp_link_vif;

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(&ahvif->dp_vif, link_id);

	dp_link_vif->tcl_metadata = u32_encode_bits(1, HTT_TCL_META_DATA_TYPE) |
				     u32_encode_bits(arvif->vdev_id,
						     HTT_TCL_META_DATA_VDEV_ID) |
				     u32_encode_bits(ar->pdev->pdev_id,
						     HTT_TCL_META_DATA_PDEV_ID);

	/* set HTT extension valid bit to 0 by default */
	dp_link_vif->tcl_metadata &= ~HTT_TCL_META_DATA_VALID_HTT;

	ath12k_dp_update_vdev_search(arvif);
	dp_link_vif->vdev_id_check_en = true;
	bank_id = ath12k_dp_tx_get_bank_profile(ab, arvif, ath12k_ab_to_dp(ab));
	dp_link_vif->bank_id = bank_id;

	/* TODO: error path for bank id failure */
	if (bank_id == DP_INVALID_BANK_ID) {
		ath12k_err(ar->ab, "Failed to initialize DP TX Banks");
		return;
	}
}

int main(void){
 struct ath12k_base base={0};struct pdev p={0};struct ath12k ar={&base,&p};struct ath12k_vif vif={0};struct ath12k_link_vif link={&vif,0,0};
 unsigned cases=0;unsigned seq[]={0,1,0,2,1,0,255,0};
 for(unsigned i=0;i<sizeof(seq)/sizeof(seq[0]);i++){
  link.vdev_id=seq[i];bank_result=3;unsigned before=bank_calls;ath12k_dp_vdev_tx_attach(&ar,&link);u32 want=1U|(seq[i]<<3);
  if(vif.dp_vif.links[0].tcl_metadata!=want||bank_metadata!=want||bank_calls!=before+1)return 42;cases++;
 }
 for(unsigned l=0;l<16;l++)for(unsigned v=0;v<256;v++)for(unsigned pd=0;pd<4;pd++){
  link.link_id=l;link.vdev_id=v;p.pdev_id=pd;vif.dp_vif.links[l].tcl_metadata=0xffffffffU;
  bank_result=(v&1)?DP_INVALID_BANK_ID:5;unsigned before=errors;
  ath12k_dp_vdev_tx_attach(&ar,&link);u32 want=1U|(v<<3)|(pd<<11);
  if(vif.dp_vif.links[l].tcl_metadata!=want||bank_metadata!=want||!vif.dp_vif.links[l].vdev_id_check_en||vif.dp_vif.links[l].bank_id!=(u8)bank_result||errors!=before+(bank_result==-1))return 43;
  cases++;
 }
 if(updates!=cases||bank_calls!=cases)return 44;
 printf("PASS cases=%u sequence_reuse_garbage_all_vdev_pdev_links_and_original_bank_error_path\n",cases);return 0;
}
