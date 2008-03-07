/* Yash: yet another shell */
/* job.c: implements job control */
/* © 2007-2008 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include "common.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "yash.h"
#include "util.h"
#include "sig.h"
#include "job.h"
#include <assert.h>


/* true なら終了時に未了のジョブに SIGHUP を送る */
bool huponexit = false;

/* ジョブのリスト。リストの要素は JOB へのポインタ。
 * リストの中の「空き」は、NULL ポインタによって示す。 */
/* これらの値はシェルの起動時に初期化される */
/* ジョブ番号が 0 のジョブはアクティブジョブ (形式的にはまだジョブリストにない
 * ジョブ) である。 */
struct plist joblist;

/* カレントジョブのジョブ番号 (joblist 内でのインデックス)
 * この値が存在しないジョブのインデックス (0 を含む) となっているとき、
 * カレントジョブは存在しない。 */
size_t currentjobnumber = 0;

/* 一時的な作業用の子プロセスの情報。普段は .jp_pid = 0。 */
struct jproc temp_chld = { .jp_pid = 0, };

/* 最後に起動したバックグラウンドジョブのプロセス ID。
 * ジョブがパイプラインになっている場合は、パイプ内の最後のプロセスの ID。
 * この値は特殊パラメータ $! の値となる。 */
pid_t last_bg_pid;

/* enum jstatus の文字列表現 */
const char *const jstatusstr[] = {
	"Running", "Done", "Stopped",
};

/* joblist を再初期化する */
void joblist_reinit(void)
{
	for (size_t i = 0; i < joblist.length; i++)
		remove_job(i);
	/*
	pl_clear(&joblist);
	pl_append(&joblist, NULL);
	*/
}

/* ジョブ制御を初期化する */
void init_jobcontrol(void)
{
	static bool initialized = false;
	if (!initialized) {
		initialized = true;
		pl_init(&joblist);
	}
	if (!joblist.length) {
		pl_append(&joblist, NULL);
	}
}

/* ジョブ制御を終了する */
void finalize_jobcontrol(void)
{
	joblist_reinit();
	huponexit = false;
	currentjobnumber = 0;
	last_bg_pid = 0;
}

/* waitpid が返す status から終了コードを得る。
 * 戻り値: status がプロセスの終了を表していない場合は -1。 */
int exitcode_from_status(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		return WTERMSIG(status) + 384;
	else
		return -1;
}

/* 指定したジョブ番号のジョブを取得する。ジョブがなければ NULL を返す。 */
JOB *get_job(size_t jobnumber)
{
	return jobnumber < joblist.length ? joblist.contents[jobnumber] : NULL;
}

/* 現在のジョブリスト内のジョブの個数を数える (アクティブジョブを除く) */
unsigned job_count(void)
{
	unsigned result = 0;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if (joblist.contents[index])
			result++;
	return result;
}

/* 現在のジョブリスト内の実行中のジョブの個数を数える(アクティブジョブを除く) */
unsigned running_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_RUNNING)
			result++;
	return result;
}

/* 現在のジョブリスト内の停止中のジョブの個数を数える(アクティブジョブを除く) */
unsigned stopped_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_STOPPED)
			result++;
	return result;
}

/* 現在のジョブリスト内の未終了ジョブの数を数える (アクティブジョブを除く) */
unsigned undone_job_count(void)
{
	unsigned result = 0;
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status != JS_DONE)
			result++;
	return result;
}

/* アクティブジョブ (ジョブ番号 0 のジョブ) をジョブリストに移動し、
 * そのジョブをカレントジョブにする。
 * 対話的シェルでは print_job_status を行う。
 * 戻り値: 成功したら新しいジョブ番号 (>0)、失敗したら -1。*/
int add_job(void)
{
	size_t jobnumber;
	JOB *job = get_job(0);

	assert(job);

	/* リストの空いているインデックスを探す */
	for (jobnumber = 1; jobnumber < joblist.length; jobnumber++)
		if (!joblist.contents[jobnumber])
			break;
	if (jobnumber == joblist.length) {  /* 空きがなかったらリストを大きくする */
		if (joblist.length == MAX_JOB) {
			xerror(0, 0, "too many jobs");
			return -1;
		} else {
			pl_append(&joblist, job);
		}
	} else {  /* 空きがあったらそこに入れる */
		assert(!joblist.contents[jobnumber]);
		joblist.contents[jobnumber] = job;
	}
	joblist.contents[0] = NULL;
	currentjobnumber = jobnumber;
	if (is_interactive_now)
		print_job_status(jobnumber, true, false);
	return jobnumber;
}

/* 指定した番号のジョブを削除する。
 * 戻り値: true なら削除成功。false はもともとジョブがなかった場合。 */
bool remove_job(size_t jobnumber)
{
	JOB *job = get_job(jobnumber);
	if (job) {
		joblist.contents[jobnumber] = NULL;
		free(job->j_procv);
		free(job->j_name);
		free(job);
		return true;
	} else {
		return false;
	}
}

/* 既に終了したジョブを削除する。(アクティブジョブを除く) */
void remove_exited_jobs(void)
{
	JOB *job;

	assert(joblist.length > 0);
	for (size_t index = joblist.length; --index > 0; )
		if ((job = joblist.contents[index]) && job->j_status == JS_DONE)
			remove_job(index);
}

/* ジョブの状態を表示する。
 * 終了したジョブ (j_status == JS_DONE) は、削除する。
 * jobnumber:   ジョブ番号。joblist のインデックス。
 *              使用されていないジョブ番号を指定した場合、何もしない。
 * changedonly: true なら変化があった場合だけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
void print_job_status(size_t jobnumber, bool changedonly, bool printpids)
{
	JOB *job;

	if (!(job = get_job(jobnumber)))
		return;

	if (!changedonly || job->j_statuschanged) {
		int estatus = job->j_waitstatus;
		if (job->j_status == JS_DONE) {
			if (WIFEXITED(estatus) && WEXITSTATUS(estatus))
				fprintf(stderr, "[%zu]%c %5d  Exit %-3d    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, WEXITSTATUS(estatus),
						job->j_name ? job->j_name : "<< unknown job >>");
			else if (WIFSIGNALED(job->j_waitstatus))
				fprintf(stderr, "[%zu]%c %5d  %-8s    %s\n",
						jobnumber, currentjobnumber == jobnumber ? '+' : ' ',
						(int) job->j_pgid, xstrsignal(WTERMSIG(estatus)),
						job->j_name ? job->j_name : "<< unknown job >>");
			else
				goto normal;
		} else {
			bool iscurrent, isbg;
normal:
			iscurrent = (currentjobnumber == jobnumber);
			isbg = (job->j_status == JS_RUNNING);
			fprintf(stderr, "[%zu]%c %5d  %-8s    %s%s\n",
					jobnumber,
					iscurrent ? '+' : ' ',
					(int) job->j_pgid, jstatusstr[job->j_status],
					job->j_name ? job->j_name : "<< unknown job >>",
					isbg ? " &" : "");
		}
		if (printpids) {
			struct jproc *ps = job->j_procv;
			for (size_t i = 0; i < job->j_procc; i++)
				fprintf(stderr, "\t* %5d  %-8s\n",
						ps[i].jp_pid, jstatusstr[ps[i].jp_status]);
		}
		job->j_statuschanged = false;
	}
	if (job->j_status == JS_DONE)
		remove_job(jobnumber);
}

/* (アクティブジョブを除く) 全てのジョブの状態を画面に出力する。
 * changedonly: true なら変化があったものだけ表示する。
 * printpids:   true なら PGID のみならず各子プロセスの PID も表示する */
void print_all_job_status(bool changedonly, bool printpids)
{
	for (size_t i = 1; i < joblist.length; i++)
		print_job_status(i, changedonly, printpids);
}

/* 子プロセスの ID からジョブ番号を得る。
 * 戻り値: ジョブ番号 (>= 0) または -1 (見付からなかった場合)。 */
int get_jobnumber_from_pid(pid_t pid)
{
	JOB *job;

	for (size_t i = 0; i < joblist.length; i++)
		if ((job = joblist.contents[i]))
			for (size_t j = 0; j < job->j_procc; j++)
				if (job->j_procv[j].jp_pid == pid)
					return i;
	return -1;
}

/* 全ての子プロセスを対象に wait する。
 * Wait するだけでジョブの状態変化の報告はしない。
 * この関数はブロックしない。 */
/* この関数は内部で SIGCHLD をブロックする。 */
void wait_chld(void)
{
	int waitpidopt;
	int status;
	pid_t pid;
	sigset_t newset, oldset;

	sigemptyset(&newset);
	sigaddset(&newset, SIGCHLD);
	sigemptyset(&oldset);
	if (sigprocmask(SIG_BLOCK, &newset, &oldset) < 0)
		xerror(0, errno, "sigprocmask before wait");

start:
	waitpidopt = WUNTRACED | WCONTINUED | WNOHANG;
	pid = waitpid(-1 /* any child process */, &status, waitpidopt);
	if (pid <= 0) {
		if (pid < 0) {
			if (errno == EINTR)
				goto start;
			if (errno != ECHILD)
				xerror(0, errno, "waitpid");
		}
		if (sigprocmask(SIG_SETMASK, &oldset, NULL) < 0)
			xerror(0, errno, "sigprocmask after wait");
		return;
	}

	size_t jobnumber;
	JOB *job;
	size_t proci;
	struct jproc *proc;
	enum jstatus oldstatus;

	/* jobnumber と、JOB の j_pids 内の PID の位置を探す */
	for (jobnumber = 0; jobnumber < joblist.length; jobnumber++)
		if ((job = joblist.contents[jobnumber]))
			for (proci = 0; proci < job->j_procc; proci++)
				if ((*(proc = job->j_procv + proci)).jp_pid == pid)
					goto pfound;
	if (temp_chld.jp_pid == pid) {
		job = NULL;
		proci = 0;
		proc = &temp_chld;
		goto pfound;
	}
	/* 未知のプロセス番号を受け取ったとき (これはジョブを disown したとき等に
	 * 起こりうる) は、単に無視して start に戻る */
	goto start;

pfound:
	if (WIFEXITED(status) || WIFSIGNALED(status)) {
		proc->jp_status = JS_DONE;
	} else if (WIFSTOPPED(status)) {
		proc->jp_status = JS_STOPPED;
	} else if (WIFCONTINUED(status)) {
		proc->jp_status = JS_RUNNING;
	}
	proc->jp_waitstatus = status;
	if (proc == &temp_chld)
		goto start;

	if (proci + 1 == job->j_procc)
		job->j_waitstatus = status;

	bool anyrunning = false, anystopped = false;
	/* ジョブの全プロセスが停止・終了したかどうか調べる */
	for (size_t i = 0; i < job->j_procc; i++) {
		switch (job->j_procv[i].jp_status) {
			case JS_RUNNING:
				anyrunning = true;
				continue;
			case JS_STOPPED:
				anystopped = true;
				continue;
			case JS_DONE:
				continue;
		}
	}
	oldstatus = job->j_status;
	job->j_status = anyrunning ? JS_RUNNING : anystopped ? JS_STOPPED : JS_DONE;
	if (job->j_status != oldstatus)
		job->j_statuschanged = true;

	goto start;
}

/* Nohup を設定していない全てのジョブに SIGHUP を送る。
 * 停止しているジョブには SIGCONT も送る。 */
void send_sighup_to_all_jobs(void)
{
	if (is_interactive_now) {
		if (temp_chld.jp_pid) {
			kill(temp_chld.jp_pid, SIGHUP);
			if (temp_chld.jp_status == JS_STOPPED)
				kill(temp_chld.jp_pid, SIGCONT);
		}
		for (size_t i = 0; i < joblist.length; i++) {
			JOB *job = joblist.contents[i];

			if (job && !(job->j_flags & JF_NOHUP)) {
				kill(-job->j_pgid, SIGHUP);
				for (size_t j = 0; j < job->j_procc; j++) {
					if (job->j_procv[i].jp_status == JS_STOPPED) {
						kill(-job->j_pgid, SIGCONT);
						break;
					}
				}
			}
		}
	} else {
		kill(0, SIGHUP);
	}
}
