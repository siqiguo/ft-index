// verify that the comparison function get a valid db object pointer

#include <sys/stat.h>
#include "test.h"

char descriptor_contents[] = "Spoon full of sugar";

const int envflags = DB_INIT_MPOOL|DB_CREATE|DB_THREAD |DB_INIT_LOCK|DB_INIT_LOG|DB_INIT_TXN|DB_PRIVATE;
char *namea="a.db";
char *nameb="b.db";

#if USE_TDB

static int my_compare(DB *UU(db), const DBT *a, const DBT *b) {
    assert(db);
    assert(db->descriptor);
    assert(db->descriptor->size == sizeof(descriptor_contents));
    assert(memcmp(db->descriptor->data, descriptor_contents, sizeof(descriptor_contents)) == 0);

    assert(a->size == b->size);
    return memcmp(a->data, b->data, a->size);
}   

#endif

static void
set_descriptor(DB* db) {
#if USE_TDB
    DBT descriptor;
    dbt_init(&descriptor, descriptor_contents, sizeof(descriptor_contents));
    int r = db->set_descriptor(db, 1, &descriptor, abort_on_upgrade);                 CKERR(r);
#endif
}

static void
do_x1_shutdown (BOOL do_commit, BOOL do_abort) {
    int r;
    r = system("rm -rf " ENVDIR); CKERR(r);
    r = toku_os_mkdir(ENVDIR, S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    r = toku_os_mkdir(ENVDIR"/data", S_IRWXU+S_IRWXG+S_IRWXO); CKERR(r);
    DB_ENV *env;
    DB *dba, *dbb;
    r = db_env_create(&env, 0);                                                         CKERR(r);
    r = env->set_data_dir(env, "data");                                                 CKERR(r);
#if USE_TDB
    r = env->set_default_bt_compare(env, my_compare);                                   CKERR(r);
#endif
    r = env->open(env, ENVDIR, envflags, S_IRWXU+S_IRWXG+S_IRWXO);                      CKERR(r);
    r = db_create(&dba, env, 0);                                                        CKERR(r);
    set_descriptor(dba);
    r = dba->open(dba, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);    CKERR(r);
    r = db_create(&dbb, env, 0);                                                        CKERR(r);
    set_descriptor(dbb);
    r = dbb->open(dbb, NULL, nameb, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);    CKERR(r);
    DB_TXN *txn;
    r = env->txn_begin(env, NULL, &txn, 0);                                             CKERR(r);
    {
	DBT a={.data="a", .size=2};
	DBT b={.data="b", .size=2};
	r = dba->put(dba, txn, &a, &b, 0);                                              CKERR(r);
	r = dba->put(dba, txn, &b, &a, 0);                                              CKERR(r);
	r = dbb->put(dbb, txn, &b, &a, 0);                                              CKERR(r);
    }
    //printf("opened\n");
    if (do_commit) {
	r = txn->commit(txn, 0);                                                        CKERR(r);
    } else if (do_abort) {
        r = txn->abort(txn);                                                            CKERR(r);
        
        // force an fsync of the log
        r = env->txn_begin(env, NULL, &txn, 0);                                         CKERR(r);
        r = txn->commit(txn, 0);                                                        CKERR(r);
    }
    //printf("shutdown\n");
    abort();
}

static void
do_x1_recover (BOOL did_commit) {
    DB_ENV *env;
    DB *dba, *dbb;
    int r;
    r = system("rm -rf " ENVDIR"/data"); /* Delete dictionaries */                          CKERR(r);
    r = toku_os_mkdir(ENVDIR"/data", S_IRWXU+S_IRWXG+S_IRWXO);                              CKERR(r);
    r = db_env_create(&env, 0);                                                             CKERR(r);
    r = env->set_data_dir(env, "data");                                                     CKERR(r);
#if USE_TDB
    r = env->set_default_bt_compare(env, my_compare);                                       CKERR(r);
#endif
    r = env->open(env, ENVDIR, envflags|DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);               CKERR(r);
    r = db_create(&dba, env, 0);                                                            CKERR(r);
    r = dba->open(dba, NULL, namea, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);        CKERR(r);
    r = db_create(&dbb, env, 0);                                                            CKERR(r);
    r = dba->open(dbb, NULL, nameb, NULL, DB_BTREE, DB_AUTO_COMMIT|DB_CREATE, 0666);        CKERR(r);
    DBT aa={.size=0}, ab={.size=0};
    DBT ba={.size=0}, bb={.size=0};
    DB_TXN *txn;
    DBC *ca,*cb;
    r = env->txn_begin(env, NULL, &txn, 0);                                                 CKERR(r);
    r = dba->cursor(dba, txn, &ca, 0);                                                      CKERR(r);
    r = dbb->cursor(dbb, txn, &cb, 0);                                                      CKERR(r);
    int ra = ca->c_get(ca, &aa, &ab, DB_FIRST);                                             CKERR(r);
    int rb = cb->c_get(cb, &ba, &bb, DB_FIRST);                                             CKERR(r);
    if (did_commit) {
	assert(ra==0);
	assert(rb==0);
	// verify key-value pairs
	assert(aa.size==2);
	assert(ab.size==2);
	assert(ba.size==2);
	assert(bb.size==2);
	const char a[2] = "a";
	const char b[2] = "b";
        assert(memcmp(aa.data, &a, 2)==0);
        assert(memcmp(ab.data, &b, 2)==0);
        assert(memcmp(ab.data, &b, 2)==0);
        assert(memcmp(bb.data, &a, 2)==0);
	assert(ca->c_get(ca, &aa, &ab, DB_NEXT) == 0);
        assert(aa.size == 2 && ab.size == 2 && memcmp(aa.data, b, 2) == 0 && memcmp(ab.data, a, 2) == 0);
	// make sure no other entries in DB
	assert(ca->c_get(ca, &aa, &ab, DB_NEXT) == DB_NOTFOUND);
	assert(cb->c_get(cb, &ba, &bb, DB_NEXT) == DB_NOTFOUND);
	fprintf(stderr, "Both verified. Yay!\n");
    } else {
	// It wasn't committed (it also wasn't aborted), but a checkpoint happened.
	assert(ra==DB_NOTFOUND);
	assert(rb==DB_NOTFOUND);
	fprintf(stderr, "Neither present. Yay!\n");
    }
    r = ca->c_close(ca);                                                                    CKERR(r);
    r = cb->c_close(cb);                                                                    CKERR(r);
    r = txn->commit(txn, 0);                                                                CKERR(r);
    r = dba->close(dba, 0);                                                                 CKERR(r);
    r = dbb->close(dbb, 0);                                                                 CKERR(r);
    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

static void
do_x1_recover_only (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                             CKERR(r);
    r = env->open(env, ENVDIR, envflags|DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);                          CKERR(r);
    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

static void
do_x1_no_recover (void) {
    DB_ENV *env;
    int r;

    r = db_env_create(&env, 0);                                                             CKERR(r);
    r = env->open(env, ENVDIR, envflags & ~DB_RECOVER, S_IRWXU+S_IRWXG+S_IRWXO);
    assert(r == DB_RUNRECOVERY);
    r = env->close(env, 0);                                                                 CKERR(r);
    exit(0);
}

const char *cmd;

#if 0

static void
do_test_internal (BOOL commit)
{
    pid_t pid;
    if (0 == (pid=fork())) {
	int r=execl(cmd, verbose ? "-v" : "-q", commit ? "--commit" : "--abort", NULL);
	assert(r==-1);
	printf("execl failed: %d (%s)\n", errno, strerror(errno));
	assert(0);
    }
    {
	int r;
	int status;
	r = waitpid(pid, &status, 0);
	//printf("signaled=%d sig=%d\n", WIFSIGNALED(status), WTERMSIG(status));
	assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
    }
    // Now find out what happend
    
    if (0 == (pid = fork())) {
	int r=execl(cmd, verbose ? "-v" : "-q", commit ? "--recover-committed" : "--recover-aborted", NULL);
	assert(r==-1);
	printf("execl failed: %d (%s)\n", errno, strerror(errno));
	assert(0);
    }
    {
	int r;
	int status;
	r = waitpid(pid, &status, 0);
	//printf("recovery exited=%d\n", WIFEXITED(status));
	assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
}

static void
do_test (void) {
    do_test_internal(TRUE);
    do_test_internal(FALSE);
}

#endif


BOOL do_commit=FALSE, do_abort=FALSE, do_explicit_abort=FALSE, do_recover_committed=FALSE,  do_recover_aborted=FALSE, do_recover_only=FALSE, do_no_recover = FALSE;

static void
x1_parse_args (int argc, char *argv[]) {
    int resultcode;
    cmd = argv[0];
    argc--; argv++;
    while (argc>0) {
	if (strcmp(argv[0], "-v") == 0) {
	    verbose++;
	} else if (strcmp(argv[0],"-q")==0) {
	    verbose--;
	    if (verbose<0) verbose=0;
	} else if (strcmp(argv[0], "--commit")==0 || strcmp(argv[0], "--test") == 0) {
	    do_commit=TRUE;
	} else if (strcmp(argv[0], "--abort")==0) {
	    do_abort=TRUE;
	} else if (strcmp(argv[0], "--explicit-abort")==0) {
	    do_explicit_abort=TRUE;
	} else if (strcmp(argv[0], "--recover-committed")==0 || strcmp(argv[0], "--recover") == 0) {
	    do_recover_committed=TRUE;
	} else if (strcmp(argv[0], "--recover-aborted")==0) {
	    do_recover_aborted=TRUE;
        } else if (strcmp(argv[0], "--recover-only") == 0) {
            do_recover_only=TRUE;
        } else if (strcmp(argv[0], "--no-recover") == 0) {
            do_no_recover=TRUE;
	} else if (strcmp(argv[0], "-h")==0) {
	    resultcode=0;
	do_usage:
	    fprintf(stderr, "Usage:\n%s [-v|-q]* [-h] {--commit | --abort | --explicit-abort | --recover-committed | --recover-aborted } \n", cmd);
	    exit(resultcode);
	} else {
	    fprintf(stderr, "Unknown arg: %s\n", argv[0]);
	    resultcode=1;
	    goto do_usage;
	}
	argc--;
	argv++;
    }
    {
	int n_specified=0;
	if (do_commit)            n_specified++;
	if (do_abort)             n_specified++;
	if (do_explicit_abort)    n_specified++;
	if (do_recover_committed) n_specified++;
	if (do_recover_aborted)   n_specified++;
	if (do_recover_only)      n_specified++;
	if (do_no_recover)        n_specified++;
	if (n_specified>1) {
	    printf("Specify only one of --commit or --abort or --recover-committed or --recover-aborted\n");
	    resultcode=1;
	    goto do_usage;
	}
    }
}

int
test_main (int argc, char *argv[])
{
    x1_parse_args(argc, argv);
    if (do_commit) {
	do_x1_shutdown (TRUE, FALSE);
    } else if (do_abort) {
	do_x1_shutdown (FALSE, FALSE);
    } else if (do_explicit_abort) {
        do_x1_shutdown(FALSE, TRUE);
    } else if (do_recover_committed) {
	do_x1_recover(TRUE);
    } else if (do_recover_aborted) {
	do_x1_recover(FALSE);
    } else if (do_recover_only) {
        do_x1_recover_only();
    } else if (do_no_recover) {
        do_x1_no_recover();
    } 
#if 0
    else {
	do_test();
    }
#endif
    return 0;
}