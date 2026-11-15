/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Actual production ordering; host tests inject operations, never call setuid.
 * XNU getgroups includes effective gid: require raw1==501, extra groups0.
 */
#ifndef IDENTITY424_H
#define IDENTITY424_H
#include <stdint.h>
#define NW424_MOBILE_ID UINT32_C(501)
enum nw424_stage { NW424_ROOT=1,NW424_CLEAR_GROUPS=2,NW424_SET_GID=3,NW424_SET_UID=4,NW424_VERIFY_IDS=5,NW424_GROUP_COUNT=6,NW424_GROUP_ONE=7,NW424_GROUP_VERIFY=8,NW424_VERIFIED=9 };
struct nw424_ids { uint32_t ruid,euid,rgid,egid; };
struct nw424_identity { unsigned stage;int ok,error,group_raw_count,extra_groups;struct nw424_ids ids; };
struct nw424_identity_ops {
 void *context;
 struct nw424_ids (*ids)(void *);
 int (*clear_groups)(void *),(*set_gid501)(void *),(*set_uid501)(void *);
 int (*group_count)(void *),(*one_group)(void *,uint32_t *),(*error)(void *);
};
static int nw424_identity_failure(const struct nw424_identity_ops *op,struct nw424_identity *r,int error) {r->error=error;r->ids=op->ids(op->context);return -1;}
static int nw424_drop_identity(const struct nw424_identity_ops *op,struct nw424_identity *r) {
 *r=(struct nw424_identity){.stage=NW424_ROOT,.group_raw_count=-1,.extra_groups=-1};
 r->ids=op->ids(op->context);
 if(r->ids.ruid!=0||r->ids.euid!=0)return nw424_identity_failure(op,r,0);
 r->stage=NW424_CLEAR_GROUPS;if(op->clear_groups(op->context)!=0){return nw424_identity_failure(op,r,op->error(op->context));}
 r->stage=NW424_SET_GID;if(op->set_gid501(op->context)!=0){return nw424_identity_failure(op,r,op->error(op->context));}
 r->stage=NW424_SET_UID;if(op->set_uid501(op->context)!=0){return nw424_identity_failure(op,r,op->error(op->context));}
 r->stage=NW424_VERIFY_IDS;r->ids=op->ids(op->context);
 if(r->ids.ruid!=NW424_MOBILE_ID||r->ids.euid!=NW424_MOBILE_ID||r->ids.rgid!=NW424_MOBILE_ID||r->ids.egid!=NW424_MOBILE_ID)return nw424_identity_failure(op,r,0);
 r->stage=NW424_GROUP_COUNT;r->group_raw_count=op->group_count(op->context);
 if(r->group_raw_count<0){return nw424_identity_failure(op,r,op->error(op->context));}
 /* Raw zero is NOT accepted as Darwin success; it is not this kernel contract. */
 if(r->group_raw_count!=1){r->stage=NW424_GROUP_VERIFY;return nw424_identity_failure(op,r,0);}
 uint32_t only=UINT32_MAX;r->stage=NW424_GROUP_ONE;
 int n=op->one_group(op->context,&only);
 if(n<0){return nw424_identity_failure(op,r,op->error(op->context));}
 r->stage=NW424_GROUP_VERIFY;if(n!=1||only!=NW424_MOBILE_ID)return nw424_identity_failure(op,r,0);
 r->extra_groups=0;r->stage=NW424_VERIFIED;r->ok=1;return 0;
}
#endif
