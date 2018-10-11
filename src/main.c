/**
 * @file main.c
 *
 * \brief Main of dummy
 *
 */

#include "api/syscall.h"
#include "api/print.h"
#include "ipc_proto.h"
#include "usb.h"
#include "usb_control.h"
#include "scsi.h"
#include "masstorage.h"
#include "api/malloc.h"

static void my_irq_handler(void);

char buffer_out[16] = "[five guys!   ]\0";
char buffer_in[16] = "               \0";

uint32_t num_tim = 0;

void tim_handler(uint8_t irq)
{
    irq = irq;
    num_tim++;
}

#define USB_BUF_SIZE 8192

uint8_t usb_buf[USB_BUF_SIZE] = { 0 };

/*
 * We use the local -fno-stack-protector flag for main because
 * the stack protection has not been initialized yet.
 */
int _main(uint32_t task_id)
{
//    const char * test = "hello, I'm usb\n";
    volatile e_syscall_ret ret = 0;
//    uint32_t size = 256;
    uint8_t id_crypto = 0;
    uint8_t id;
    struct sync_command ipc_sync_cmd;
    dma_shm_t dmashm_rd;
    dma_shm_t dmashm_wr;
#if 0
    int i = 0;
    uint64_t tick = 0;
#endif

    printf("Hello ! I'm usb, my id is %x\n", task_id);

    ret = sys_init(INIT_GETTASKID, "crypto", &id_crypto);
    printf("crypto is task %x !\n", id_crypto);

    /*********************************************
     * Declaring DMA Shared Memory with Crypto
     *********************************************/
    dmashm_rd.target = id_crypto;
    dmashm_rd.source = task_id;
    dmashm_rd.address = (physaddr_t)usb_buf;
    dmashm_rd.size = USB_BUF_SIZE;
    /* Crypto DMA will read from this buffer */
    dmashm_rd.mode = DMA_SHM_ACCESS_RD;

    dmashm_wr.target = id_crypto;
    dmashm_wr.source = task_id;
    dmashm_wr.address = (physaddr_t)usb_buf;
    dmashm_wr.size = USB_BUF_SIZE;
    /* Crypto DMA will write into this buffer */
    dmashm_wr.mode = DMA_SHM_ACCESS_WR;

    printf("Declaring DMA_SHM for SDIO read flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_rd);
    printf("sys_init returns %s !\n", strerror(ret));

    printf("Declaring DMA_SHM for SDIO write flow\n");
    ret = sys_init(INIT_DMA_SHM, &dmashm_wr);
    printf("sys_init returns %s !\n", strerror(ret));

    /* initialize the SCSI stack with two buffers of 4096 bits length each. */
    scsi_early_init(usb_buf, USB_BUF_SIZE);

    /*******************************************
     * End of init
     *******************************************/

    ret = sys_init(INIT_DONE);
    printf("sys_init DONE returns %x !\n", ret);


    /*******************************************
     * let's syncrhonize with other tasks
     *******************************************/
    logsize_t size = 2;

    printf("sending end_of_init syncrhonization to crypto\n");
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_CMD;
    ipc_sync_cmd.state = SYNC_READY;

    do {
      ret = 42;
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (const char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("Oops ! ret = %d\n", ret);
      } else {
          printf("end of end_of_init synchro.\n");
      }
    } while (ret != SYS_E_DONE);

    /* Now wait for Acknowledge from Smart */
    id = id_crypto;

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("ack from crypto: Oops ! ret = %d\n", ret);
      } else {
          printf("Aclknowledge from crypto ok\n");
      }
    } while (ret != SYS_E_DONE);
    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_RESP
        && ipc_sync_cmd.state == SYNC_ACKNOWLEDGE) {
        printf("crypto has acknowledge end_of_init, continuing\n");
    }

    /*******************************************
     * Starting end_of_cryp synchronization
     *******************************************/

    printf("waiting end_of_cryp syncrhonization from crypto\n");

    id = id_crypto;
    size = 2;

    do {
        ret = sys_ipc(IPC_RECV_SYNC, &id, &size, (char*)&ipc_sync_cmd);
    } while (ret == SYS_E_BUSY);

    if (   ipc_sync_cmd.magic == MAGIC_TASK_STATE_CMD
        && ipc_sync_cmd.state == SYNC_READY) {
        printf("crypto module is ready\n");
    }

    /* Initialize USB device */
    wmalloc_init();
    ipc_sync_cmd.magic = MAGIC_TASK_STATE_RESP;
    ipc_sync_cmd.state = SYNC_READY;

    size = 2;
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, size, (char*)&ipc_sync_cmd);
      if (ret != SYS_E_DONE) {
          printf("sending Sync ready to crypto: Oops ! ret = %d\n", ret);
      } else {
          printf("sending sync ready to crypto ok\n");
      }
    } while (ret == SYS_E_BUSY);

    // take some time to finish all sync ipc...
    sys_sleep(2000, SLEEP_MODE_INTERRUPTIBLE);

    /*******************************************
     * Sharing DMA SHM address and size with crypto
     *******************************************/
    struct dmashm_info {
        uint32_t addr;
        uint16_t size;
    };
    struct dmashm_info dmashm_info;

    dmashm_info.addr = (uint32_t)usb_buf;
    dmashm_info.size = USB_BUF_SIZE;

    printf("informing crypto about DMA SHM...\n");
    do {
      ret = sys_ipc(IPC_SEND_SYNC, id_crypto, sizeof(struct dmashm_info), (char*)&dmashm_info);
    } while (ret == SYS_E_BUSY);
    printf("Crypto informed.\n");

    /*******************************************
     * End of init sequence, let's initialize devices
     *******************************************/

    scsi_init();
    mass_storage_init();


    /*******************************************
     * Starting USB listener
     *******************************************/

    printf("USB main loop starting\n");

    scsi_state_machine(id_crypto, id_crypto);

    while (1) {
        sys_yield();
    }
    /* should return to do_endoftask() */
    return 0;
}

/* this is the uart IRQ handler for UART implem */
static void my_irq_handler(void)
{

}
