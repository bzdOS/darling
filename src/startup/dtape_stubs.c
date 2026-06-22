/* dtape_stubs.c — Stub implementations of all darlingserver duct-tape (XNU)
 * functions for the FreeBSD port.  These allow the binary to link and start;
 * Mach IPC / XNU kernel-emulation calls will return ENOSYS / NULL / false.
 * Replace with real duct-tape implementation once it is ported to FreeBSD. */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>

/* Pull in all type and function declarations from duct-tape.h so our stub
 * definitions match the expected signatures exactly. */
#include <darlingserver/duct-tape.h>

/* ── init ──────────────────────────────────────────────────────────────────── */

void dtape_init(const dtape_hooks_t* hooks) {
    (void)hooks;
    fprintf(stderr, "[dtape-stub] dtape_init called (noop)\n");
}

void dtape_init_in_thread(void) {}
void dtape_deinit(void) {}

/* ── traps — return port name 0 (MACH_PORT_NULL) ───────────────────────────── */

uint32_t dtape_task_self_trap(void)                { return 0; }
uint32_t dtape_host_self_trap(void)                { return 0; }
uint32_t dtape_thread_self_trap(void)              { return 0; }
uint32_t dtape_mach_reply_port(void)               { return 0; }
uint32_t dtape_thread_get_special_reply_port(void) { return 0; }
uint32_t dtape_mk_timer_create(void)               { return 0; }

/* ── task / thread lifetime ─────────────────────────────────────────────────── */

dtape_task_t* dtape_task_create(dtape_task_t* parent, uint32_t nsid,
                                void* ctx, dserver_rpc_architecture_t arch)
{ (void)parent; (void)nsid; (void)ctx; (void)arch; return NULL; }

dtape_thread_t* dtape_thread_create(dtape_task_t* task, uint64_t nsid, void* ctx)
{ (void)task; (void)nsid; (void)ctx; return NULL; }

void dtape_thread_entering(dtape_thread_t* t)           { (void)t; }
void dtape_thread_exiting(dtape_thread_t* t)            { (void)t; }
void dtape_thread_dying(dtape_thread_t* t)              { (void)t; }
void dtape_thread_retain(dtape_thread_t* t)             { (void)t; }
void dtape_thread_release(dtape_thread_t* t)            { (void)t; }
void dtape_thread_set_handles(dtape_thread_t* t, uintptr_t ph, uintptr_t dq)
{ (void)t; (void)ph; (void)dq; }

dtape_thread_t* dtape_thread_for_port(uint32_t p)      { (void)p; return NULL; }
void* dtape_thread_context(dtape_thread_t* t)           { (void)t; return NULL; }
int dtape_thread_load_state_from_user(dtape_thread_t* t, uintptr_t ts, uintptr_t fs)
{ (void)t; (void)ts; (void)fs; return -1; }
int dtape_thread_save_state_to_user(dtape_thread_t* t, uintptr_t ts, uintptr_t fs)
{ (void)t; (void)ts; (void)fs; return -1; }
void dtape_thread_process_signal(dtape_thread_t* t, int bsd_sig, int linux_sig,
                                 int code, uintptr_t addr)
{ (void)t; (void)bsd_sig; (void)linux_sig; (void)code; (void)addr; }
void dtape_thread_wait_while_user_suspended(dtape_thread_t* t) { (void)t; }
void dtape_thread_sigexc_enter(dtape_thread_t* t)       { (void)t; }
void dtape_thread_sigexc_exit(dtape_thread_t* t)        { (void)t; }
void dtape_thread_sigexc_enter2(dtape_thread_t* t)      { (void)t; }

void dtape_task_uidgid(dtape_task_t* t, int nu, int ng, int* ou, int* og)
{ (void)t; (void)nu; (void)ng; if(ou)*ou=-1; if(og)*og=-1; }
void dtape_task_retain(dtape_task_t* t)                 { (void)t; }
void dtape_task_release(dtape_task_t* t)                { (void)t; }
void dtape_task_dying(dtape_task_t* t)                  { (void)t; }
void dtape_task_set_dyld_info(dtape_task_t* t, uint64_t a, uint64_t l)
{ (void)t; (void)a; (void)l; }
void dtape_task_set_sigexc_enabled(dtape_task_t* t, bool e) { (void)t; (void)e; }
bool dtape_task_try_resume(dtape_task_t* t)             { (void)t; return false; }

void dtape_timer_fired(void) {}

/* ── kqchan ─────────────────────────────────────────────────────────────────── */

dtape_kqchan_mach_port_t* dtape_kqchan_mach_port_create(
    dtape_task_t* task, uint32_t port,
    uint64_t rbuf, uint64_t rbufsz, uint64_t flags,
    dtape_kqchan_mach_port_notification_callback_f cb, void* ctx)
{ (void)task;(void)port;(void)rbuf;(void)rbufsz;(void)flags;(void)cb;(void)ctx; return NULL; }

void dtape_kqchan_mach_port_destroy(dtape_kqchan_mach_port_t* k) { (void)k; }
void dtape_kqchan_mach_port_modify(dtape_kqchan_mach_port_t* k,
    uint64_t rbuf, uint64_t rbufsz, uint64_t flags)
{ (void)k;(void)rbuf;(void)rbufsz;(void)flags; }
void dtape_kqchan_mach_port_disable_notifications(dtape_kqchan_mach_port_t* k) { (void)k; }
bool dtape_kqchan_mach_port_fill(dtape_kqchan_mach_port_t* k,
    dserver_kqchan_reply_mach_port_read_t* r, uint64_t dbuf, uint64_t dbufsz)
{ (void)k;(void)r;(void)dbuf;(void)dbufsz; return false; }
bool dtape_kqchan_mach_port_has_events(dtape_kqchan_mach_port_t* k) { (void)k; return false; }

/* ── semaphore ──────────────────────────────────────────────────────────────── */

dtape_semaphore_t* dtape_semaphore_create(dtape_task_t* t, int v)
{ (void)t; (void)v; return NULL; }
void dtape_semaphore_destroy(dtape_semaphore_t* s)      { (void)s; }
void dtape_semaphore_up(dtape_semaphore_t* s)           { (void)s; }
bool dtape_semaphore_down_simple(dtape_semaphore_t* s)  { (void)s; return false; }

/* ── DSERVER_DTAPE_DECLS — all return KERN_FAILURE (5) ────────────────────── */
/* These are the Mach trap RPC wrappers; return 5 = KERN_FAILURE. */

int dtape_mach_msg_overwrite(uint64_t msg, int32_t option, uint32_t send_size,
    uint32_t rcv_size, uint32_t rcv_name, uint32_t timeout, uint32_t priority,
    uint64_t rcv_msg)
{ (void)msg;(void)option;(void)send_size;(void)rcv_size;(void)rcv_name;
  (void)timeout;(void)priority;(void)rcv_msg; return 5; }

int dtape_mach_port_deallocate(uint32_t target, uint32_t name)
{ (void)target;(void)name; return 5; }
int dtape_mach_port_allocate(uint32_t target, int32_t right, uint64_t name)
{ (void)target;(void)right;(void)name; return 5; }
int dtape_mach_port_mod_refs(uint32_t target, uint32_t name, int32_t right, int32_t delta)
{ (void)target;(void)name;(void)right;(void)delta; return 5; }
int dtape_mach_port_move_member(uint32_t target, uint32_t member, uint32_t after)
{ (void)target;(void)member;(void)after; return 5; }
int dtape_mach_port_insert_right(uint32_t target, uint32_t name, uint32_t poly, int32_t polyPoly)
{ (void)target;(void)name;(void)poly;(void)polyPoly; return 5; }
int dtape_mach_port_insert_member(uint32_t target, uint32_t name, uint32_t pset)
{ (void)target;(void)name;(void)pset; return 5; }
int dtape_mach_port_extract_member(uint32_t target, uint32_t name, uint32_t pset)
{ (void)target;(void)name;(void)pset; return 5; }
int dtape_mach_port_construct(uint32_t target, uint64_t options, uint64_t context, uint64_t name)
{ (void)target;(void)options;(void)context;(void)name; return 5; }
int dtape_mach_port_destruct(uint32_t target, uint32_t name, int32_t srdelta, uint64_t guard)
{ (void)target;(void)name;(void)srdelta;(void)guard; return 5; }
int dtape_mach_port_guard(uint32_t target, uint32_t name, uint64_t guard, bool strict)
{ (void)target;(void)name;(void)guard;(void)strict; return 5; }
int dtape_mach_port_unguard(uint32_t target, uint32_t name, uint64_t guard)
{ (void)target;(void)name;(void)guard; return 5; }
int dtape_mach_port_request_notification(uint32_t target, uint32_t name, int32_t msgid,
    uint32_t sync, uint32_t notify, uint32_t notifyPoly, uint64_t previous)
{ (void)target;(void)name;(void)msgid;(void)sync;(void)notify;(void)notifyPoly;(void)previous; return 5; }
int dtape_mach_port_get_attributes(uint32_t target, uint32_t name, int32_t flavor,
    uint64_t info, uint64_t count)
{ (void)target;(void)name;(void)flavor;(void)info;(void)count; return 5; }
int dtape_mach_port_type(uint32_t target, uint32_t name, uint64_t ptype)
{ (void)target;(void)name;(void)ptype; return 5; }
int dtape_task_for_pid(uint32_t target_tport, int32_t pid, uint64_t t)
{ (void)target_tport;(void)pid;(void)t; return 5; }
int dtape_task_name_for_pid(uint32_t target_tport, int32_t pid, uint64_t t)
{ (void)target_tport;(void)pid;(void)t; return 5; }
int dtape_pid_for_task(uint32_t t, uint64_t pid)
{ (void)t;(void)pid; return 5; }
int dtape_mach_vm_allocate(uint32_t target, uint64_t addr, uint64_t size, int32_t flags)
{ (void)target;(void)addr;(void)size;(void)flags; return 5; }
int dtape_mach_vm_deallocate(uint32_t target, uint64_t address, uint64_t size)
{ (void)target;(void)address;(void)size; return 5; }
int dtape_semaphore_signal(uint32_t signal_name)        { (void)signal_name; return 5; }
int dtape_semaphore_signal_all(uint32_t signal_name)    { (void)signal_name; return 5; }
int dtape_semaphore_wait(uint32_t wait_name)            { (void)wait_name; return 5; }
int dtape_semaphore_wait_signal(uint32_t wait_name, uint32_t signal_name)
{ (void)wait_name;(void)signal_name; return 5; }
int dtape_semaphore_timedwait(uint32_t wait_name, uint32_t sec, uint32_t nsec)
{ (void)wait_name;(void)sec;(void)nsec; return 5; }
int dtape_semaphore_timedwait_signal(uint32_t wait_name, uint32_t signal_name,
    uint32_t sec, uint32_t nsec)
{ (void)wait_name;(void)signal_name;(void)sec;(void)nsec; return 5; }
int dtape_mk_timer_destroy(uint32_t name)               { (void)name; return 5; }
int dtape_mk_timer_arm(uint32_t name, uint64_t expire_time)
{ (void)name;(void)expire_time; return 5; }
int dtape_mk_timer_cancel(uint32_t name, uint64_t result_time)
{ (void)name;(void)result_time; return 5; }
int dtape_psynch_cvbroad(uint64_t cv, uint64_t cvlsgen, uint64_t cvudgen, uint32_t flags,
    uint64_t mutex, uint64_t mugen, uint64_t tid, uint32_t* retval)
{ (void)cv;(void)cvlsgen;(void)cvudgen;(void)flags;(void)mutex;(void)mugen;(void)tid;
  if(retval)*retval=0; return 5; }
int dtape_psynch_cvclrprepost(uint64_t cv, uint32_t cvgen, uint32_t cvugen, uint32_t cvsgen,
    uint32_t prepocnt, uint32_t preposeq, uint32_t flags, uint32_t* retval)
{ (void)cv;(void)cvgen;(void)cvugen;(void)cvsgen;(void)prepocnt;(void)preposeq;(void)flags;
  if(retval)*retval=0; return 5; }
int dtape_psynch_cvsignal(uint64_t cv, uint64_t cvlsgen, uint32_t cvugen, int32_t threadport,
    uint64_t mutex, uint64_t mugen, uint64_t tid, uint32_t flags, uint32_t* retval)
{ (void)cv;(void)cvlsgen;(void)cvugen;(void)threadport;(void)mutex;(void)mugen;(void)tid;
  (void)flags; if(retval)*retval=0; return 5; }
int dtape_psynch_cvwait(uint64_t cv, uint64_t cvlsgen, uint32_t cvugen, uint64_t mutex,
    uint64_t mugen, uint32_t flags, int64_t sec, uint32_t nsec, uint32_t* retval)
{ (void)cv;(void)cvlsgen;(void)cvugen;(void)mutex;(void)mugen;(void)flags;(void)sec;(void)nsec;
  if(retval)*retval=0; return 5; }
int dtape_psynch_mutexdrop(uint64_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid,
    uint32_t flags, uint32_t* retval)
{ (void)mutex;(void)mgen;(void)ugen;(void)tid;(void)flags; if(retval)*retval=0; return 5; }
int dtape_psynch_mutexwait(uint64_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid,
    uint32_t flags, uint32_t* retval)
{ (void)mutex;(void)mgen;(void)ugen;(void)tid;(void)flags; if(retval)*retval=0; return 5; }
int dtape_psynch_rw_rdlock(uint64_t rwlock, uint32_t lgenval, uint32_t ugenval,
    uint32_t rw_wc, int32_t flags, uint32_t* retval)
{ (void)rwlock;(void)lgenval;(void)ugenval;(void)rw_wc;(void)flags; if(retval)*retval=0; return 5; }
int dtape_psynch_rw_unlock(uint64_t rwlock, uint32_t lgenval, uint32_t ugenval,
    uint32_t rw_wc, int32_t flags, uint32_t* retval)
{ (void)rwlock;(void)lgenval;(void)ugenval;(void)rw_wc;(void)flags; if(retval)*retval=0; return 5; }
int dtape_psynch_rw_wrlock(uint64_t rwlock, uint32_t lgenval, uint32_t ugenval,
    uint32_t rw_wc, int32_t flags, uint32_t* retval)
{ (void)rwlock;(void)lgenval;(void)ugenval;(void)rw_wc;(void)flags; if(retval)*retval=0; return 5; }

/* ── debug ──────────────────────────────────────────────────────────────────── */

uint64_t dtape_debug_task_port_count(dtape_task_t* t)  { (void)t; return 0; }
uint64_t dtape_debug_task_list_ports(dtape_task_t* t,
    dtape_debug_task_list_ports_iterator_f it, void* ctx)
{ (void)t;(void)it;(void)ctx; return 0; }
uint64_t dtape_debug_portset_list_members(dtape_task_t* t, uint32_t ps,
    dtape_debug_portset_list_members_iterator_f it, void* ctx)
{ (void)t;(void)ps;(void)it;(void)ctx; return 0; }
uint64_t dtape_debug_port_list_messages(dtape_task_t* t, uint32_t port,
    dtape_debug_port_list_messages_iterator_f it, void* ctx)
{ (void)t;(void)port;(void)it;(void)ctx; return 0; }
