
#define __boot		__attribute__ ((__section__ (".boot.text")))
#define __boottdata	__attribute__ ((__section__ (".boot.data")))

/* For assembly routines */
#define __BOOT		.section	".boot.text","ax"
#define __BOOTDATA	.section	".boot.data","aw"

