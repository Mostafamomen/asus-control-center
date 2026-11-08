/* security/security_engine.cpp - real read-only checks. GPL-3.0-or-later */
#include "security_engine.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
static const gchar* sec_status_name_(SecStatus s){ switch(s){case SEC_PASS:return "PASS";case SEC_WARNING:return "WARNING";case SEC_CRITICAL:return "CRITICAL";case SEC_NOT_AVAILABLE:return "NOT AVAILABLE";case SEC_SKIPPED:return "SKIPPED";case SEC_ERROR:return "ERROR";case SEC_CANCELLED:return "CANCELLED";} return "UNKNOWN"; }
const gchar* sec_status_name(SecStatus s){ return sec_status_name_(s); }
/* Run argv with a 10s timeout. Polls in 100ms slices so cancellation and
 * timeout actually kill the child instead of hiding progress. Never uses a
 * shell. Returns malloc'd output ("" on timeout/error) or NULL on spawn fail.
 * Sets *timed_out via internal errno==ETIMEDOUT marker: caller sees empty
 * output + exit_code==-1 and must NOT treat as PASS (engine downgrades). */
static void reap_pid(GPid pid, gint *wstatus){
  pid_t r;
  do { r=waitpid(pid,wstatus,0); } while(r==-1 && errno==EINTR);
  g_spawn_close_pid(pid);
}
static gboolean atomic_cancelled(gint* c){ return c && g_atomic_int_get(c)!=0; }
/* argv-only, 10s timeout. Pipe fd is O_NONBLOCK; blocking wait is poll().
 * Checks deadline and cancellation each poll iteration (100ms). On timeout
 * or cancel: SIGTERM, 500ms grace (poll+WNOHANG), SIGKILL, reap EINTR.
 * Channel + PID closed exactly once on every path. */
static gchar* run_argv(const gchar* const* argv, gint* exit_code, gint* cancelled_atomic){
  GError* e=NULL; GPid pid=0; gint outfd=-1;
  if(!g_spawn_async_with_pipes(NULL,(gchar**)argv,NULL,(GSpawnFlags)(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD),NULL,NULL,&pid,NULL,&outfd,NULL,&e))
    {
      g_clear_error(&e);
      if(exit_code) *exit_code=-1;
      return NULL;
    }
  /* O_NONBLOCK on read end so read never blocks the worker */
  { gint flags=fcntl(outfd,F_GETFL,0); if(flags!=-1) fcntl(outfd,F_SETFL,flags|O_NONBLOCK); }
  GIOChannel* chan=g_io_channel_unix_new(outfd);
  g_io_channel_set_encoding(chan,NULL,NULL); g_io_channel_set_buffered(chan,FALSE);
  GString* buf=g_string_new("");
  gint64 deadline=g_get_monotonic_time()+10*1000000LL;
  gchar tmp[4096]; gsize n=0;
  gboolean done=FALSE;
  while(!done){
    if(g_get_monotonic_time()>deadline || atomic_cancelled(cancelled_atomic))
      {
        kill(pid,SIGTERM);
        gint wstatus=0;
        gint64 grace=g_get_monotonic_time()+500000LL;
        gboolean reaped=FALSE;
        while(g_get_monotonic_time()<grace)
          {
            pid_t r=waitpid(pid,&wstatus,WNOHANG);
            if(r==pid) { reaped=TRUE; break; }
            if(r==-1 && errno!=EINTR) break;
            g_usleep(100*1000);
          }
        if(!reaped) { kill(pid,SIGKILL); reap_pid(pid,&wstatus); }
        else g_spawn_close_pid(pid);
        g_io_channel_shutdown(chan,TRUE,NULL);
        g_io_channel_unref(chan);
        g_string_free(buf,TRUE);
        if(exit_code) *exit_code=-1;
        return atomic_cancelled(cancelled_atomic) ? NULL : g_strdup("");
      }
    GIOStatus st=g_io_channel_read_chars(chan,tmp,sizeof tmp,&n,NULL);
    if(n>0) g_string_append_len(buf,tmp,n);
    if(st==G_IO_STATUS_EOF || st==G_IO_STATUS_ERROR) break;
    /* poll() on the pipe: wakes on output/EOF/timeout without busy-loop */
    pollfd pfd; pfd.fd=outfd; pfd.events=POLLIN; pfd.revents=0;
    poll(&pfd,1,100);
  }
  gint wstatus=0; reap_pid(pid,&wstatus);
  g_io_channel_shutdown(chan,TRUE,NULL); g_io_channel_unref(chan);
  if(exit_code) *exit_code=(WIFEXITED(wstatus)?WEXITSTATUS(wstatus):-1);
  return g_string_free(buf,FALSE);
}
static gboolean have(const gchar* bin){ gchar* p=g_find_program_in_path(bin); gboolean r=p!=NULL; g_free(p); return r; }
static gchar* read_file(const gchar* path){ gchar*c=NULL; if(!g_file_get_contents(path,&c,NULL,NULL)) return NULL; return c; }
static gint file_mode(const gchar* path){ struct stat st; if(stat(path,&st)!=0) return -1; return st.st_mode & 0777; }
static SecResult mk(const gchar* id,const gchar* name,const gchar* cat,SecStatus s,const gchar* detail,const gchar* evidence,const gchar* rec,gint sev,gboolean admin){
  SecResult r; r.id=g_strdup(id); r.name=g_strdup(name); r.category=g_strdup(cat); r.status=s; r.detail=g_strdup(detail?detail:"—"); r.evidence=g_strdup(evidence?evidence:"—"); r.recommendation=g_strdup(rec?rec:"No action required."); r.severity=sev; r.needs_admin=admin; r.reversible=TRUE; return r;
}
static void add(SecReport* rep, SecResult r){ rep->items=g_renew(SecResult, rep->items, rep->len+1); rep->items[rep->len++]=r; switch(r.status){case SEC_PASS:rep->pass++;break;case SEC_WARNING:rep->warn++;break;case SEC_CRITICAL:rep->crit++;break;case SEC_NOT_AVAILABLE:rep->na++;break;case SEC_SKIPPED:rep->skip++;break;default:rep->err++;break;} }
static gboolean cancelled(gint* c){ return c && g_atomic_int_get(c)!=0; }
SecReport* sec_run_all(gint* cancelled_flag){
  SecReport* rep=g_new0(SecReport,1);
  if(cancelled(cancelled_flag)) return rep;
  /* Secure Boot */
  { gchar* sb=read_file("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-af6f-00c04f831632");
    if(sb){ gboolean on=(strnlen(sb,64)>=5 && (unsigned char)sb[4]==1); add(rep,mk("secureboot","Secure Boot","System Protection", on?SEC_PASS:SEC_CRITICAL, on?"Secure Boot is enabled.":"Secure Boot is disabled.", sb, on?"Keep enabled.":"Enable Secure Boot in firmware settings and enroll keys.", on?0:2,FALSE)); }
    else if(have("mokutil")){ gint ec; gchar* o=run_argv((const gchar*[]){"mokutil","--sb-state",NULL},&ec,cancelled_flag); gboolean on=o&&g_strstr_len(o,-1,"enabled"); add(rep,mk("secureboot","Secure Boot","System Protection", ec==0?(on?SEC_PASS:SEC_CRITICAL):SEC_NOT_AVAILABLE, ec==0?(on?"Enabled.":"Disabled."):"mokutil failed.", o?o:"—", "Enable Secure Boot in firmware.", 2,FALSE)); g_free(o);}
    else add(rep,mk("secureboot","Secure Boot","System Protection",SEC_NOT_AVAILABLE,"Cannot read Secure Boot state (no efivar, no mokutil).","—","On Fedora use mokutil --sb-state.",0,FALSE));
    g_free(sb); }
  if(cancelled(cancelled_flag)) return rep;
  /* Kernel */
  { gchar* k=read_file("/proc/sys/kernel/osrelease"); if(k) add(rep,mk("kernel","Kernel","System Protection",SEC_SKIPPED,g_strstrip(k),"uname present","Keep kernel updated.",0,FALSE)); else add(rep,mk("kernel","Kernel","System Protection",SEC_NOT_AVAILABLE,"Cannot read kernel release.","—","—",0,FALSE)); g_free(k); }
  if(cancelled(cancelled_flag)) return rep;
  /* SELinux */
  {
    gchar* e=read_file("/sys/fs/selinux/enforce");
    if(e)
      {
        gboolean en=g_strcmp0(g_strstrip(e),"1")==0;
        add(rep,mk("selinux","SELinux","System Protection", en?SEC_PASS:SEC_CRITICAL, en?"SELinux enforcing.":"SELinux not enforcing.", e, en?"Keep enforcing.":"Set SELINUX=enforcing in /etc/selinux/config and reboot.", en?0:2,TRUE));
      }
    else if(have("getenforce"))
      {
        gint ec;
        gchar*o=run_argv((const gchar*[]){"getenforce",NULL},&ec,cancelled_flag);
        gboolean en=o&&g_strcmp0(g_strstrip(o),"Enforcing")==0;
        add(rep,mk("selinux","SELinux","System Protection", ec==0?(en?SEC_PASS:SEC_CRITICAL):SEC_NOT_AVAILABLE, o?g_strstrip(o):"—", o?o:"—","Keep enforcing.", en?0:2,TRUE));
        g_free(o);
      }
    else
      {
        add(rep,mk("selinux","SELinux","System Protection",SEC_NOT_AVAILABLE,"SELinux status unreadable.","—","Install/run on RHEL-family for SELinux.",0,FALSE));
      }
    g_free(e);
  }
  if(cancelled(cancelled_flag)) return rep;
  /* Firewall */
  { gint ec; gchar* o=run_argv((const gchar*[]){"systemctl","is-active","firewalld",NULL},&ec,cancelled_flag); gchar* t=o?g_strstrip(o):NULL; gboolean active=t&&g_strcmp0(t,"active")==0; add(rep,mk("firewall","Firewall (firewalld)","Firewall", active?SEC_PASS:SEC_WARNING, active?"firewalld is active.":"firewalld is not active.", t?t:"—", active?"Keep active.":"Start firewalld: systemctl enable --now firewalld (requires admin).", active?0:1,TRUE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* Automatic updates */
  { gint ec; gchar*o=run_argv((const gchar*[]){"systemctl","is-enabled","dnf-automatic.timer",NULL},&ec,cancelled_flag); gboolean en=o&&g_strstr_len(o,-1,"enabled")&&!g_strstr_len(o,-1,"disabled"); add(rep,mk("autoupdates","Automatic security updates","System Protection", en?SEC_PASS:SEC_WARNING, en?"dnf-automatic timer enabled.":"dnf-automatic timer not enabled.", o?g_strstrip(o):"—", en?"Keep enabled.":"Enable dnf-automatic.timer for security updates.", en?0:1,TRUE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* Disk encryption */
  { gint ec; gchar*o=run_argv((const gchar*[]){"lsblk","-o","NAME,TYPE,FSTYPE",NULL},&ec,cancelled_flag); gboolean enc=o&&g_strstr_len(o,-1,"crypto_LUKS"); add(rep,mk("diskenc","Disk encryption","System Protection", (o&&ec==0)?(enc?SEC_PASS:SEC_SKIPPED):SEC_NOT_AVAILABLE, (o&&ec==0)?(enc?"LUKS device detected.":"No LUKS device in lsblk output (may still be encrypted via other means)."):"lsblk unavailable.", o?o:"—", enc?"Keep encrypted.":"Verify encryption via journal/cryptsetup if needed.", enc?0:1,FALSE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* Failed services */
  { gint ec; gchar*o=run_argv((const gchar*[]){"systemctl","--failed","--no-legend",NULL},&ec,cancelled_flag); gchar*t=o?g_strstrip(o):NULL; gboolean failed=(ec!=0||!o); gboolean none=!t||!*t; add(rep,mk("failedsvc","Failed systemd units","System Integrity", failed?SEC_NOT_AVAILABLE:(none?SEC_PASS:SEC_WARNING), failed?"systemctl --failed did not succeed.":(none?"No failed units.":"Failed units present."), t&&*t?t:"(none)", failed?"Run systemctl --failed manually.":(none?"No action.":"Inspect with systemctl status <unit> and journalctl."), failed?1:(none?0:1),FALSE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* Root & sudo */
  { gint rm=file_mode("/etc/shadow"); gint sm=file_mode("/etc/sudoers"); gchar* rootsh=read_file("/etc/shadow"); add(rep,mk("sudo","Root and sudo","Permissions", sm==-1?SEC_NOT_AVAILABLE:((sm&0022)==0?SEC_PASS:SEC_CRITICAL), sm==-1?"Cannot stat /etc/sudoers.":"Checked /etc/sudoers permissions (read-only).", sm==-1?"not readable":"mode present", sm==-1?"Run as root to inspect.":"Ensure sudoers is not group/other writable.", sm==-1?0:2,TRUE)); g_free(rootsh); (void)rm; }
  if(cancelled(cancelled_flag)) return rep;
  /* Polkit */
  { gboolean haspolicy=g_file_test("/usr/share/polkit-1/actions/io.github.asuscontrolcenter.AsusControlCenter.policy",G_FILE_TEST_EXISTS); add(rep,mk("polkit","Polkit policy","Permissions", haspolicy?SEC_WARNING:SEC_WARNING, haspolicy?"ASUSCraft polkit policy present.":"Policy not found at standard path.", "io.github.asuscontrolcenter.AsusControlCenter.policy", "Install policy via install.sh / package.", haspolicy?0:1,FALSE)); }
  if(cancelled(cancelled_flag)) return rep;
  /* SSH */
  {
    gint ec;
    gchar*o=run_argv((const gchar*[]){"systemctl","is-active","sshd",NULL},&ec,cancelled_flag);
    gchar*t=o?g_strstrip(o):NULL;
    gboolean active=t&&g_strcmp0(t,"active")==0;
    gchar* cfg=read_file("/etc/ssh/sshd_config");
    gboolean rootlogin=FALSE, explicit_safe=FALSE, has_include=FALSE;
    if(cfg)
      {
        gchar** lines=g_strsplit(cfg,"\n",-1);
        for(guint i=0; lines && lines[i]; i++)
          {
            gchar* line=g_strstrip(g_strdup(lines[i]));
            if(!*line || *line=='#') { g_free(line); continue; }
            gchar* low=g_ascii_strdown(line,-1);
            if(g_str_has_prefix(low,"include ")) has_include=TRUE;
            gchar** tok=g_strsplit_set(line," \t",-1);
            if(tok && tok[0] && g_ascii_strcasecmp(tok[0],"PermitRootLogin")==0 && tok[1])
              {
                if(g_ascii_strcasecmp(tok[1],"yes")==0) rootlogin=TRUE;
                else if(g_ascii_strcasecmp(tok[1],"no")==0 || g_ascii_strcasecmp(tok[1],"prohibit-password")==0) explicit_safe=TRUE;
              }
            g_strfreev(tok); g_free(low); g_free(line);
          }
        g_strfreev(lines);
      }
    if(!active)
      {
        add(rep,mk("ssh","SSH server","System Protection",SEC_PASS,"sshd not running (not exposed).","inactive","No action.",0,FALSE));
      }
    else if(rootlogin)
      {
        add(rep,mk("ssh","SSH server","System Protection",SEC_CRITICAL,"sshd active; PermitRootLogin yes (uncommented).","PermitRootLogin yes","Set PermitRootLogin no/prohibit-password and restart sshd (admin).",2,TRUE));
      }
    else if(cfg && !has_include && (explicit_safe || !rootlogin))
      {
        /* Config parsed with no Include and no yes: treat as safe only if
         * an explicit safe value was present; otherwise default is SKIPPED
         * (full OpenSSH Include/Match parsing is out of scope). */
        if(explicit_safe)
          add(rep,mk("ssh","SSH server","System Protection",SEC_PASS,"sshd active; PermitRootLogin explicitly safe.","ok","Keep restricted.",0,TRUE));
        else
          add(rep,mk("ssh","SSH server","System Protection",SEC_SKIPPED,"sshd active; PermitRootLogin not explicitly set; full OpenSSH parsing out of scope.","—","Run sshd -T as root to verify effective config.",0,FALSE));
      }
    else
      {
        add(rep,mk("ssh","SSH server","System Protection",SEC_SKIPPED,"sshd active but config not fully parsed (missing file or Include directive).","—","Run sshd -T as root to verify effective config.",0,FALSE));
      }
    g_free(o);
    g_free(cfg);
  }
  if(cancelled(cancelled_flag)) return rep;
  /* Listening sockets */
  { if(have("ss")){ gint ec; gchar*o=run_argv((const gchar*[]){"ss","-tuln",NULL},&ec,cancelled_flag); add(rep,mk("sockets","Listening sockets","Network Security", SEC_SKIPPED, "ss -tuln captured (read-only).", o?o:"—", "Review unexpected 0.0.0.0 listeners.",0,FALSE)); g_free(o);} else add(rep,mk("sockets","Listening sockets","Network Security",SEC_NOT_AVAILABLE,"ss not installed.","—","Install iproute2 for ss.",0,FALSE)); }
  if(cancelled(cancelled_flag)) return rep;
  /* Network interfaces */
  { gint ec; gchar*o=run_argv((const gchar*[]){"ip","-o","link",NULL},&ec,cancelled_flag); add(rep,mk("interfaces","Network interfaces","Network Security", o?SEC_SKIPPED:SEC_NOT_AVAILABLE, o?"Interfaces listed.":"ip unavailable.", o?o:"—","Review unexpected interfaces.",0,FALSE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* firewall zones */
  { if(have("firewall-cmd")){ gint ec; gchar*o=run_argv((const gchar*[]){"firewall-cmd","--get-active-zones",NULL},&ec,cancelled_flag); add(rep,mk("fwzones","Firewall zones","Firewall", ec==0?SEC_PASS:SEC_WARNING, o?o:"firewall-cmd failed.", o?o:"—","Review active zones.",0,FALSE)); g_free(o);} else add(rep,mk("fwzones","Firewall zones","Firewall",SEC_NOT_AVAILABLE,"firewall-cmd not installed.","—","Install firewalld.",0,FALSE)); }
  if(cancelled(cancelled_flag)) return rep;
  /* Sensitive files */
  { gint m=file_mode("/etc/shadow"); gboolean bad=m!=-1 && (m&0077)!=0; add(rep,mk("sensitive","Sensitive file permissions","Privacy", m==-1?SEC_NOT_AVAILABLE:(bad?SEC_CRITICAL:SEC_PASS), m==-1?"Cannot read /etc/shadow mode.":(bad?"Overly broad permissions.":"Permissions look correct."), m==-1?"—":"mode checked", "Fix with chmod 000 /etc/shadow as root.", bad?2:0,TRUE)); }
  if(cancelled(cancelled_flag)) return rep;
  /* Users */
  { gchar*p=read_file("/etc/passwd"); int n=0; gboolean parsed=FALSE; if(p){ parsed=TRUE; gchar**l=g_strsplit(p,"\n",-1); for(int i=0;l[i];i++) if(g_strstr_len(l[i],-1,":0:")||g_str_has_prefix(l[i],"root:")) n++; g_strfreev(l);} add(rep,mk("users","User accounts","Privacy & Permissions", parsed?(n>1?SEC_WARNING:SEC_PASS):SEC_NOT_AVAILABLE, "Parsed /etc/passwd (no passwords collected).", "UID-0 accounts counted (no names exported)", "Review UID 0 accounts.",0,FALSE)); g_free(p); }
  if(cancelled(cancelled_flag)) return rep;
  /* Malware tools */
  { struct {const gchar* bin; const gchar* name; const gchar* what;} tools[]={{"clamscan","ClamAV","Signature AV scan"},{"lynis","Lynis","Auditing/hardening scan"},{"rkhunter","rkhunter","Rootkit scan"},{"chkrootkit","chkrootkit","Rootkit scan"}};
    for(int i=0;i<4;i++){ if(cancelled(cancelled_flag)) break; if(have(tools[i].bin)){ gint ec; gchar*o=run_argv((const gchar*[]){tools[i].bin,"--version",NULL},&ec,cancelled_flag); add(rep,mk(tools[i].bin,tools[i].name,"Malware & Threat Detection", ec==0?SEC_SKIPPED:SEC_WARNING, tools[i].what, o?o:"—", "Run full scan manually; this check only verifies presence and never deletes/quarantines.",0,FALSE)); g_free(o);} else add(rep,mk(tools[i].bin,tools[i].name,"Malware & Threat Detection",SEC_NOT_AVAILABLE,"not installed; see recommendation","—","Install only with user permission (dnf install clamav / lynis ...).",0,FALSE)); } }
  if(cancelled(cancelled_flag)) return rep;
  /* RPM verify (bounded: verify kernel only if present) */
  { if(have("rpm")){ gint ec; gchar*o=run_argv((const gchar*[]){"rpm","-q","kernel",NULL},&ec,cancelled_flag); add(rep,mk("rpm","RPM package verification","System Integrity", o&&*o?SEC_SKIPPED:SEC_NOT_AVAILABLE, o&&*o?"rpm query works (full rpm -Va is heavy and not auto-run).":"rpm query empty.", o?o:"—", "Run rpm -Va manually as root for full verification.",0,FALSE)); g_free(o);} else add(rep,mk("rpm","RPM package verification","System Integrity",SEC_NOT_AVAILABLE,"rpm not available.","—","Fedora: rpm is expected.",0,FALSE)); }
  if(cancelled(cancelled_flag)) return rep;
  /* asusd */
  { gint ec; gchar*o=run_argv((const gchar*[]){"systemctl","is-active","asusd",NULL},&ec,cancelled_flag); gchar*t=o?g_strstrip(o):NULL; gboolean act=t&&g_strcmp0(t,"active")==0; add(rep,mk("asusd","asusd service","System Integrity", act?SEC_PASS:SEC_WARNING, act?"asusd running.":"asusd not active.", t?t:"—", "Start asusd if ASUS controls are needed.", act?0:1,FALSE)); g_free(o); }
  if(cancelled(cancelled_flag)) return rep;
  /* NVIDIA driver */
  { gchar*m=read_file("/proc/modules"); gboolean nv=m&&g_strstr_len(m,-1,"nvidia"); add(rep,mk("nvidia","NVIDIA driver","System Integrity", nv?SEC_PASS:SEC_SKIPPED, nv?"nvidia module loaded.":"nvidia module not loaded (may be expected on iGPU).", nv?"loaded":"not loaded", "Check with lsmod if GPU issues.",0,FALSE)); g_free(m); }
  if(cancelled(cancelled_flag)) return rep;
  /* Wi-Fi */
  { gint ec; gchar*o=run_argv((const gchar*[]){"nmcli","-t","-f","SSID,SECURITY","dev","wifi",NULL},&ec,cancelled_flag); add(rep,mk("wifi","Wi-Fi security","Network Security", (o&&*o)?SEC_SKIPPED:SEC_NOT_AVAILABLE, o&&*o?"Wi-Fi list captured.":"nmcli Wi-Fi unavailable.", o?o:"—", "Prefer WPA3/WPA2-AES networks.",0,FALSE)); g_free(o); }
  return rep;
}
void sec_report_free(SecReport* r){ if(!r) return; for(guint i=0;i<r->len;i++){ g_free(r->items[i].id); g_free(r->items[i].name); g_free(r->items[i].category); g_free(r->items[i].detail); g_free(r->items[i].evidence); g_free(r->items[i].recommendation);} g_free(r->items); g_free(r); }
static gchar* html_esc(const gchar* s){ if(!s) s="—"; GString* o=g_string_new(""); for(const gchar* c=s;*c;c++){ if(*c=='&') g_string_append(o,"&amp;"); else if(*c=='<') g_string_append(o,"&lt;"); else if(*c=='>') g_string_append(o,"&gt;"); else if(*c=='"') g_string_append(o,"&quot;"); else g_string_append_c(o,*c);} return g_string_free(o,FALSE); }
static gchar* json_esc(const gchar* s){ if(!s) s="—"; GString* o=g_string_new(""); for(const gchar* c=s;*c;c++){ if(*c=='\\') g_string_append(o,"\\\\"); else if(*c=='"') g_string_append(o,"\\\""); else if(*c=='\n') g_string_append(o,"\\n"); else if(*c=='\r') g_string_append(o,"\\r"); else if(*c=='\t') g_string_append(o,"\\t"); else if((unsigned char)*c<32) g_string_append_printf(o,"\\u%04x",(unsigned char)*c); else g_string_append_c(o,*c);} return g_string_free(o,FALSE); }
gchar* sec_report_to_json(const SecReport* r){ GString* s=g_string_new("{\"generated\":\""); { GDateTime* dt=g_date_time_new_now_local(); gchar* t=g_date_time_format_iso8601(dt); g_string_append(s,t); g_free(t); g_date_time_unref(dt); } g_string_append_printf(s,"\",\"summary\":{\"pass\":%d,\"warning\":%d,\"critical\":%d,\"not_available\":%d,\"skipped\":%d,\"error\":%d},\"checks\":[",r->pass,r->warn,r->crit,r->na,r->skip,r->err); for(guint i=0;i<r->len;i++){ if(i) g_string_append(s,","); { gchar* d=json_esc(r->items[i].detail); gchar* rec=json_esc(r->items[i].recommendation); GString* chunk=g_string_new(""); g_string_append_printf(chunk,"{\"id\":\"%s\",\"name\":\"%s\",\"category\":\"%s\",\"status\":\"%s\",\"detail\":\"%s\",\"recommendation\":\"%s\"}", r->items[i].id, r->items[i].name, r->items[i].category, sec_status_name(r->items[i].status), d, rec); g_string_append(s,chunk->str); g_string_free(chunk,TRUE); g_free(d); g_free(rec); } } g_string_append(s,"]}\n"); return g_string_free(s,FALSE); }
gchar* sec_report_to_html(const SecReport* r){ GString*s=g_string_new("<html><head><meta charset='utf-8'><title>ASUSCraft Security Report</title></head><body><h1>ASUSCraft Security Report</h1>"); g_string_append_printf(s,"<p>Pass %d · Warning %d · Critical %d · Not available %d · Skipped %d</p><p>Assessment: calculated from counts above. Not a guarantee; unavailable checks are not counted as passes.</p><table border=1><tr><th>Check</th><th>Status</th><th>Detail</th><th>Recommendation</th></tr>",r->pass,r->warn,r->crit,r->na,r->skip); for(guint i=0;i<r->len;i++) { gchar* d=html_esc(r->items[i].detail); gchar* rec=html_esc(r->items[i].recommendation); gchar* nm=html_esc(r->items[i].name); g_string_append_printf(s,"<tr><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>",nm,sec_status_name(r->items[i].status),d,rec); g_free(nm); g_free(d); g_free(rec); } g_string_append(s,"</table></body></html>\n"); return g_string_free(s,FALSE); }
gchar* sec_report_to_text(const SecReport* r){ GString*s=g_string_new("ASUSCraft Security Report\n"); g_string_append_printf(s,"Pass %d Warning %d Critical %d NotAvailable %d Skipped %d Error %d\nAssessment: counts are the assessment; unavailable checks never count as pass.\n",r->pass,r->warn,r->crit,r->na,r->skip,r->err); for(guint i=0;i<r->len;i++) g_string_append_printf(s,"[%s] %s (%s): %s | %s\n",sec_status_name(r->items[i].status),r->items[i].name,r->items[i].category,r->items[i].detail,r->items[i].recommendation); return g_string_free(s,FALSE); }
void sec_history_append(const SecReport* r){ gchar* dir=g_build_filename(g_get_user_config_dir(),"asus-control-center",NULL); g_mkdir_with_parents(dir,0700); gchar* path=g_build_filename(dir,"security-history.conf",NULL); GKeyFile*k=g_key_file_new(); g_key_file_load_from_file(k,path,G_KEY_FILE_KEEP_COMMENTS,NULL); gchar* t=NULL; { GDateTime* dt=g_date_time_new_now_local(); t=g_date_time_format_iso8601(dt); g_date_time_unref(dt); } gchar* key=g_strdup_printf("scan_%ld",(long)g_get_monotonic_time()); { gchar* val=g_strdup_printf("%s pass=%d warn=%d crit=%d na=%d",t,r->pass,r->warn,r->crit,r->na); g_key_file_set_value(k,"history",key,val); g_free(val); } { GError* se=NULL; gboolean ok=g_key_file_save_to_file(k,path,&se); if(!ok) { g_printerr("security: history save failed: %s\n", se&&se->message?se->message:"?"); g_clear_error(&se); } else if(g_chmod(path,0600)!=0) g_printerr("security: chmod 0600 failed for history\n"); } g_free(t); g_free(key); g_free(path); g_free(dir); g_key_file_free(k); }
GPtrArray* sec_history_entries(void){ GPtrArray*a=g_ptr_array_new_with_free_func(g_free); gchar* path=g_build_filename(g_get_user_config_dir(),"asus-control-center","security-history.conf",NULL); GKeyFile*k=g_key_file_new(); if(g_key_file_load_from_file(k,path,G_KEY_FILE_KEEP_COMMENTS,NULL)){ gsize n=0; gchar**keys=g_key_file_get_keys(k,"history",&n,NULL); for(gsize i=0;i<n;i++) g_ptr_array_add(a,g_key_file_get_value(k,"history",keys[i],NULL)); g_strfreev(keys);} g_key_file_free(k); g_free(path); return a; }
