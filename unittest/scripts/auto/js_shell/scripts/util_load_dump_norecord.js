

function EXPECT_FILE_NOT_EXISTS(path) {
  try {
    os.loadTextFile(path);
    testutil.fail(path+" expected to not exist but it does.");
  } catch(error) {
    // OK
  }
}


function EXPECT_FILE_EXISTS(path) {
  try {
    os.loadTextFile(path);
  } catch(error) {
    testutil.fail(path+" expected to exist but it doesn't.");
  }
}

function load_progress(path) {
  var entries=os.loadTextFile(path).split("\n");
  var r=[];
  for(i in entries) {
    entry=entries[i];
    if(entry=="") continue;
    entry=JSON.parse(entry);
    r.push(entry);
  }
  return r;
}

function check_load_progress(path) {
  var entries=load_progress(path);
  var table_ddl={};
  var table_data={};

  for(i in entries) {
    entry=entries[i];

    if(entry.op=="TABLE-DDL") {
      key=entry.schema+"."+entry.table;
      if(entry.done)
        EXPECT_TRUE(table_ddl[key]==false);
      else
        EXPECT_TRUE(table_ddl[key]===undefined);
      table_ddl[key]=entry.done;
    } else if(entry.op=="TABLE-DATA") {
      key=entry.schema+"."+entry.table+"."+entry.chunk;
      if(entry.done)
        EXPECT_TRUE(table_data[key]==false);
      else
        EXPECT_TRUE(table_data[key]===undefined);
      table_data[key]=entry.done;
    } else {
      continue;
    }
  }

  failed=false;

  for(i in table_ddl) {
    if(!table_ddl[i]) {
      println(i, "DDL is not marked done");
      failed=true;
    }
  }
  for(i in table_data) {
    if(!table_data[i]) {
      println(i, "data is not marked done");
      failed=true;
    }
  }
  EXPECT_FALSE(failed);
}

function check_load_progress_all_data_failed(path) {
  var entries=load_progress(path);

  failed=false;
  for(i in entries) {
    entry=entries[i];

    if(entry.op=="TABLE-DATA"&&entry.done) {
      println(entry.schema+"."+entry.table+"."+entry.chunk, "is marked done");
    }
  }

  EXPECT_FALSE(failed);
}

function check_worker_distribution(nthreads, ntables, path) {
  // check that all tables finish loading at about the same time
  var entries=load_progress(path);
  var loaded=[];

  for(i in entries) {
    entry=entries[i];

    if(entry.op=="TABLE-DATA") {
      // keep a list of the last ntables*2 tables that were loaded
      if(entry.done)
        loaded.push(entry.table);
      if(loaded.length>ntables*2)
        delete loaded[0];
    }
  }

  check={};

  // ensure that the list of loaded tables have at least 1 of each table
  // ideally we'd be checking against the last nthreads tables, but we double that
  // to give some room for randomness
  for(t in loaded) {
    t=loaded[t];
    check[t]=true;
  }
  for(i=0; i<ntables; i++) {
    if(check["table"+i]!=true) {
      print("Table distribution across workers is not well balanced:");
      print(check);
      print();
      print(os.loadTextFile(path));
      EXPECT_TRUE(false);
      break;
    }
  }
}

// on Windows, replace all backslashes with slashes
var filename_for_file=__os_type=="windows"? function (filename) {return filename.replace(/\\/g, "/");}:function (filename) {return filename;};

// on Windows, replace all slashes with backslashes
var filename_for_output=__os_type=="windows"? function (filename) {return filename.replace(/\//g, "\\");}:function (filename) {return filename;};

//@<> INCLUDE dump_utils.inc

//@<> Setup
try {testutil.rmdir(__tmp_dir+"/ldtest", true);} catch {}
testutil.mkdir(__tmp_dir+"/ldtest");
testutil.mkdir(__tmp_dir+"/ldtest/datadirs");
testutil.mkdir(__tmp_dir+"/ldtest/datadirs/testindexdir");
if(__version_num<80000) {
  // 8.0 seems to create dirs automatically but not 5.7
  testutil.mkdir(__tmp_dir+"/ldtest/datadirs/testindexdir");
  testutil.mkdir(__tmp_dir+"/ldtest/datadirs/test datadir");
}

testutil.deploySandbox(__mysql_sandbox_port1, "root", {
  loose_innodb_directories: __tmp_dir+"/ldtest/datadirs",
  early_plugin_load: "keyring_file."+(__os_type=="windows"? "dll":"so"),
  keyring_file_data: __tmp_dir+"/ldtest/datadirs/keyring"
});

shell.connect(__sandbox_uri1);


// change root password
session.runSql("SET PASSWORD FOR root@localhost = 'old'");
session.runSql("SET PASSWORD FOR root@'%' = 'old'");
old_sandbox_uri1="mysql://root:old@localhost:"+__mysql_sandbox_port1;
shell.connect(old_sandbox_uri1);

session.runSql("create schema world");
// load test schemas
testutil.importData(old_sandbox_uri1, __data_path+"/sql/fieldtypes_all.sql", "", "utf8mb4"); // xtest
testutil.importData(old_sandbox_uri1, __data_path+"/sql/sakila-schema.sql"); // sakila
testutil.importData(old_sandbox_uri1, __data_path+"/sql/sakila-data.sql", "sakila"); // sakila
testutil.importData(old_sandbox_uri1, __data_path+"/sql/world.sql", "world");
testutil.importData(old_sandbox_uri1, __data_path+"/sql/misc_features.sql"); // misc_features
testutil.preprocessFile(__data_path+"/sql/mysqlaas_compat.sql",
  ["TMPDIR="+filename_for_file(__tmp_dir+"/ldtest/datadirs")], __tmp_dir+"/ldtest/mysqlaas_compat.sql",
  ['-- Tablespace dumping not supported in v1']);
// Need to retry this one a few times because ENCRYPTION option requires keyring to be loaded,
// but it may not yet be loaded.
ok=false;
for(i=0; i<5; i++) {
  try {
    testutil.importData(old_sandbox_uri1, __tmp_dir+"/ldtest/mysqlaas_compat.sql"); // mysqlaas_compat
    ok=true;
    break;
  } catch(error) {
    println(error);
    // can fail if the keyring plugin didn't load yet, so we just retry
    os.sleep(1);
    println("Retrying...");
  }
}
EXPECT_TRUE(ok);

// create test accounts
session.runSql("CREATE USER 'admin user'@'%' IDENTIFIED BY 'abrakadabra'");
session.runSql("GRANT ALL ON test.* TO 'admin user'@'%' WITH GRANT OPTION");

session.runSql("CREATE USER loader@'%'");
session.runSql("GRANT ALL ON *.* TO loader@'%' WITH GRANT OPTION");

// create the dump

util.dumpInstance(__tmp_dir+"/ldtest/dump", {bytesPerChunk: "128k"});

util.dumpInstance(__tmp_dir+"/ldtest/dump-nogz", {compression: "none"});

util.dumpInstance(__tmp_dir+"/ldtest/dump-nochunk", {chunking: false});

// create snapshot of the instance to compare later
var reference=snapshot_instance(session);

shell.connect("mysqlx://root:old@localhost:"+__mysql_sandbox_x_port1);
session.runSql("create schema test");
util.importJson(__import_data_path+'/primer-dataset-id.json', {schema: "test"});
session.runSql("analyze table test.`primer-dataset-id`");
util.dumpInstance(__tmp_dir+"/ldtest/dump-big", {bytesPerChunk: "128k"});

var reference_big=snapshot_instance(session);

function EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session, reference_snapshot) {
  if(reference_snapshot===undefined)
    reference_snapshot=reference;
  EXPECT_JSON_EQ(strip_keys(strip_snapshot_data(reference_snapshot), ["accounts"]), strip_keys(strip_snapshot_data(snapshot_instance(session)), ["accounts"]));
}

function EXPECT_DUMP_LOADED(session) {
  EXPECT_JSON_EQ(strip_snapshot_data(reference), strip_snapshot_data(snapshot_instance(session)));
}

function EXPECT_NO_CHANGES(session, before) {
  EXPECT_JSON_EQ(before, snapshot_instance(session));
}


// recreate the instance, wipe-out the datadir
testutil.destroySandbox(__mysql_sandbox_port1);
testutil.rmdir(__tmp_dir+"/ldtest/datadirs", true);
testutil.mkdir(__tmp_dir+"/ldtest/datadirs");
if(__version_num<80000) {
  testutil.mkdir(__tmp_dir+"/ldtest/datadirs/testindexdir");
  testutil.mkdir(__tmp_dir+"/ldtest/datadirs/test datadir");
}
testutil.deploySandbox(__mysql_sandbox_port1, "root", {
  loose_innodb_directories: __tmp_dir+"/ldtest/datadirs",
  early_plugin_load: "keyring_file."+(__os_type=="windows"? "dll":"so"),
  plugin_load: "mysqlx."+(__os_type=="windows"? "dll":"so"),
  keyring_file_data: __tmp_dir+"/ldtest/datadirs/keyring",
  log_bin: 1
});

shell.connect(__sandbox_uri1);
uuid=session.runSql("select @@server_uuid").fetchOne()[0];

// update the reference snapshot making the root passwords match the ones in the sandbox
// the root account isn't supposed to be changed
reference["accounts"]["root@localhost"]["create"]=session.runSql("show create user root@localhost").fetchOne()[0];
reference["accounts"]["root@%"]["create"]=session.runSql("show create user root@'%'").fetchOne()[0];

// General usage
// -------------
// TSFR1_1, TSFR2_1, TSFR2_2, TSFR4_2, TSFR5_2, TSFR6_2, TSFR9_3

//@<> load a dump with no session
session.close();
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Util.loadDump: An open session is required to perform this operation.");

shell.connect(__sandbox_uri1);

//@<> Try to load the dump with local-infile disabled (should fail)

// local_infile is on by default in 5.7
if(__version_num<80000) session.runSql("SET GLOBAL local_infile=0");

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Util.loadDump: local_infile disabled in server");
EXPECT_OUTPUT_CONTAINS("ERROR: The 'local_infile' global system variable must be set to ON in the target server, after the server is verified to be trusted.");

//@<> Enable local-infile for the remaining tests
session.runSql("SET GLOBAL local_infile=1");

//@ Bad Options (should fail)
util.loadDump(null);
util.loadDump(123);
util.loadDump({path: "/tmp/dumps"});
util.loadDump(__tmp_dir+"/ldtest/dump/@.sql");
util.loadDump(__tmp_dir+"/ldtest/");
util.loadDump(__tmp_dir+"/ldtest/", null);
util.loadDump(__tmp_dir+"/ldtest/", 123);
util.loadDump(__tmp_dir+"/ldtest/", {}, 456);
util.loadDump(__tmp_dir+"/ldtest/dump", {osNamespace: "ns"});
util.loadDump(__tmp_dir+"/ldtest/dump", {ociConfigFile: "/badpath"});
util.loadDump(__tmp_dir+"/ldtest/dump", {ociConfigFile: "/badpath", osBucketName: "bukkit"});
util.loadDump(__tmp_dir+"/ldtest/dump", {bogus: 1234});
util.loadDump(__tmp_dir+"/ldtest/dump", {includeSchemas: "*"});
util.loadDump(__tmp_dir+"/ldtest/dump", {loadDdl: 0, loadData: 0});
util.loadDump(__tmp_dir+"/ldtest/dump", {loadDdl: 0, loadData: 0, loadUsers: 0});
util.loadDump(__tmp_dir+"/ldtest/dump", {waitDumpTimeout: "???"});
util.loadDump(__tmp_dir+"/ldtest/dump", {analyzeTables: true});
util.loadDump(__tmp_dir+"/ldtest/dump", {analyzeTables: ""});
util.loadDump(__tmp_dir+"/ldtest/dump", {analyzeTables: "xxx"});

//@ Bad Bucket Name Option
util.loadDump(__tmp_dir+"/ldtest/dump", {osBucketName: "bukkit"});

//@ Plain load of plain dump (compressed and chunked)
old_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];

util.loadDump(__tmp_dir+"/ldtest/dump");

// users not loaded, so don't compare accounts list
EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

// GTIDs should be logged normally by default or when skipBinlog:false
// TSFR15_5 (default skipBinlog:false changes GTID set)
new_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];
EXPECT_NE(old_gtid_executed, new_gtid_executed);

// Various checks on the contents of the dump
var done_file=__tmp_dir+"/ldtest/dump/@.done.json";
EXPECT_TRUE(os.loadTextFile(done_file)!="");

//@<> Try loading again, which should not do anything because the progress file says it's all done
check_load_progress(__tmp_dir+"/ldtest/dump/load-progress."+uuid+".json");

util.loadDump(__tmp_dir+"/ldtest/dump");

EXPECT_OUTPUT_NOT_CONTAINS("Executing DDL script");
EXPECT_OUTPUT_NOT_CONTAINS("[Worker");
EXPECT_OUTPUT_NOT_CONTAINS("Executing triggers");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);


// TODO TSFR2_4, TSFR2_5, TSFR2_6, TSFR2_7, TSFR2_8, FR13

//@<> Plain load of uncompressed dump
// TSFR2_3 also use file:// 
// also check progressFile option
// also check that the defaultCharacterSet is taken from the dump file - TSFR14_4
session.runSql("set global character_set_server = 'sjis'");
session.runSql("set global character_set_connection = 'sjis'");
session.runSql("set global character_set_client = 'sjis'");
session.runSql("set global character_set_results = 'sjis'");
util.loadDump("file://"+__tmp_dir+"/ldtest/dump-nogz", {progressFile: __tmp_dir+"/ldtest/load-progress.txt"});

EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

// TSFR10_2
EXPECT_OUTPUT_CONTAINS("using 4 threads");

testutil.rmfile(__tmp_dir+"/ldtest/dump-nogz/load-progress*");
wipe_instance(session);

//@<> Plain load of non-chunked dump
util.loadDump(__tmp_dir+"/ldtest/dump-nochunk");

// users not loaded, so don't compare accounts list
EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

testutil.rmfile(__tmp_dir+"/ldtest/dump-nochunk/load-progress*");
wipe_instance(session);

//@<> Load with users (prepare)
// create the loader user without some grants and different password, to verify the acct didn't change after load
session.runSql("CREATE USER loader@'%' IDENTIFIED BY 'secret'");
session.runSql("GRANT ALL ON *.* TO loader@'%' WITH GRANT OPTION");
session.runSql("GRANT PROXY ON ''@'' TO loader@'%' WITH GRANT OPTION");

//@<> Load with users - revoke clone_admin on 8.0 {VER(>=8.0.17)}
session.runSql("REVOKE clone_admin ON *.* FROM loader@'%'");

//@<> Load with users
var loader_account=snapshot_account(session, "loader", "%");

// also check non-std sql_mode and charset
var old_sql_mode=session.runSql("select @@sql_mode").fetchOne()[0];
session.runSql("set global sql_mode='ANSI_QUOTES,NO_AUTO_VALUE_ON_ZERO,NO_BACKSLASH_ESCAPES,NO_DIR_IN_CREATE,NO_ZERO_DATE,PAD_CHAR_TO_FULL_LENGTH'");
session.runSql("set global character_set_server='latin1'");

shell.connect("mysql://loader:secret@127.0.0.1:"+__mysql_sandbox_port1);

util.loadDump(__tmp_dir+"/ldtest/dump", {loadUsers: true});

EXPECT_OUTPUT_CONTAINS("NOTE: Skipping CREATE/ALTER USER statements for current user loader@%");
EXPECT_OUTPUT_CONTAINS("NOTE: Skipping GRANT statements for current user loader@%");

session.runSql("set global sql_mode=?", [old_sql_mode]);
session.runSql("set sql_mode=?", [old_sql_mode]);

// check that loader@% has the old grants and passwd and not the ones from the dump
EXPECT_JSON_EQ(loader_account, snapshot_account(session, "loader", "%"));

var snapshot=snapshot_instance(session);

var reference_modified=JSON.parse(JSON.stringify(reference));
reference_modified["accounts"]["loader@%"]=loader_account;

EXPECT_JSON_EQ(reference_modified, snapshot);

// connect root using expected password (instead of 'old')
shell.connect("mysql://root:root@localhost:"+__mysql_sandbox_port1);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);
session.runSql("set global sql_mode=default");

shell.connect(__sandbox_uri1);

//@<> Load DDL and Users only
// TSFR4_3, TSFR5_1, TSFR6_1
util.loadDump(__tmp_dir+"/ldtest/dump", {loadUsers: true, loadDdl: true, loadData: false});

EXPECT_JSON_EQ(strip_snapshot_data(reference), strip_snapshot_data(snapshot_instance(session)));
testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");

//@<> Load Data only for sakila.country and sakila.city (schema already exists)
// no users
// TSFR4_1, TSFR5_3, TSFR6_3
var orig_accounts=snapshot_accounts(session);

// ignoreExistingObjects shouldn't be needed since we're specifying loadData only
util.loadDump(__tmp_dir+"/ldtest/dump", {loadUsers: false, loadDdl: false, loadData: true, includeTables: ["sakila.country", "sakila.city"]});

var snap=snapshot_instance(session);

EXPECT_EQ(null, snap["schemas"]["sakila"]["tables"]["film"]["count"]);
EXPECT_JSON_EQ(reference["schemas"]["sakila"]["tables"]["country"], snap["schemas"]["sakila"]["tables"]["country"]);
EXPECT_JSON_EQ(reference["schemas"]["sakila"]["tables"]["city"], snap["schemas"]["sakila"]["tables"]["city"]);

EXPECT_EQ(orig_accounts, snap["accounts"]);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> includeSchemas
// TSFR7_1
util.loadDump(__tmp_dir+"/ldtest/dump", {includeSchemas: ["sakila", "mysqlaas_compat", "bogus"]});

var snap=snapshot_instance(session);
EXPECT_JSON_EQ(["mysqlaas_compat", "sakila"], Object.keys(snap["schemas"]).sort());
EXPECT_JSON_EQ(reference["schemas"]["sakila"], snap["schemas"]["sakila"]);
EXPECT_JSON_EQ(reference["schemas"]["mysqlaas_compat"], snap["schemas"]["mysqlaas_compat"]);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> includeTables
// TSFR8_1, TSFR8_2
util.loadDump(__tmp_dir+"/ldtest/dump", {includeTables: ["sakila.staff", "xtest.t_json", "bogus.bla"]});

var snap=snapshot_instance(session);

EXPECT_EQ(["sakila", "xtest"], Object.keys(snap["schemas"]).sort());
EXPECT_EQ(["staff"], Object.keys(snap["schemas"]["sakila"]["tables"]));
EXPECT_EQ(["t_json"], Object.keys(snap["schemas"]["xtest"]["tables"]));
EXPECT_JSON_EQ(reference["schemas"]["sakila"]["tables"]["staff"], snap["schemas"]["sakila"]["tables"]["staff"]);
EXPECT_JSON_EQ(reference["schemas"]["xtest"]["tables"]["t_json"], snap["schemas"]["xtest"]["tables"]["t_json"]);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> excludeSchemas
// TSFR17_1, TSFR17_3
util.loadDump(__tmp_dir+"/ldtest/dump", {excludeSchemas: ["xtest", "bogus", "world"]});

var snap=snapshot_instance(session);

EXPECT_EQ(["all_features", "mysqlaas_compat", "sakila"], Object.keys(snap["schemas"]).sort());

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> excludeTables
// TSFR16_1, TSFR16_3
util.loadDump(__tmp_dir+"/ldtest/dump", {excludeTables: ["sakila.film", "sakila.actor_info", "sakila.film_list", "sakila.nicer_but_slower_film_list", "sakila.sales_by_film_category", "`xtest`.`t_json`"]});

var snap=snapshot_instance(session);

EXPECT_EQ(Object.keys(reference["schemas"]).sort(), Object.keys(snap["schemas"]).sort());
EXPECT_EQ(["actor", "address", "category", "city", "country", "customer", "film_actor", "film_category", "film_text", "inventory", "language", "payment", "rental", "staff", "store"], Object.keys(snap["schemas"]["sakila"]["tables"]).sort());
EXPECT_EQ(["customer_list", "sales_by_store", "staff_list"], Object.keys(snap["schemas"]["sakila"]["views"]).sort());
EXPECT_EQ(["t_bigint", "t_bit", "t_char", "t_date", "t_decimal1", "t_decimal2", "t_decimal3", "t_double", "t_enum", "t_float", "t_geom", "t_geom_all", "t_int", "t_integer", "t_lchar", "t_lob", "t_mediumint", "t_numeric1", "t_numeric2", "t_real", "t_set", "t_smallint", "t_tinyint"], Object.keys(snap["schemas"]["xtest"]["tables"]).sort());

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> includeTable + excludeSchema

// this should result in sakila.film loaded
util.loadDump(__tmp_dir+"/ldtest/dump", {excludeSchemas: ["sakila"], includeTables: ["sakila.film"]});

var snap=snapshot_instance(session);

EXPECT_EQ([], Object.keys(snap["schemas"]));

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> load dump where some objects already exist
session.runSql("create schema sakila");
session.runSql("create table sakila.film (a int primary key)");
session.runSql("create view sakila.staff_list as select 1");
session.runSql("CREATE TRIGGER sakila.ins_film AFTER INSERT ON sakila.film FOR EACH ROW BEGIN END;");
session.runSql("create schema mysqlaas_compat");
session.runSql("CREATE FUNCTION mysqlaas_compat.func2 () RETURNS INT NO SQL SQL SECURITY DEFINER RETURN 0;");
session.runSql("CREATE PROCEDURE mysqlaas_compat.proc2 () NO SQL SQL SECURITY DEFINER BEGIN END;");
session.runSql("CREATE EVENT mysqlaas_compat.event2 ON SCHEDULE EVERY 1 DAY DO BEGIN END;");

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1});}, "Util.loadDump: Duplicate objects found in destination database");

//@<> load dump where some objects already exist, but exclude them from the load
util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1, excludeSchemas: ["sakila", "mysqlaas_compat"]});

util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1, excludeTables: ["sakila.film", "sakila.staff_list"], excludeSchemas: ["mysqlaas_compat"]});

//@<> load dump where some objects already exist, but exclude them by loading only unrelated parts with includeSchema
util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1, includeSchemas: ["xtest"]});

//@<> load dump where some objects already exist + ignoreExistingObjects
util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1, ignoreExistingObjects: true});

EXPECT_OUTPUT_CONTAINS("NOTE: One or more objects in the dump already exist in the destination database but will be ignored because the 'ignoreExistingObjects' option was enabled.");

//@<> no dryRun to get errors from mismatched definitions {VER(>=8.0.0)}
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {ignoreExistingObjects: true});}, "Util.loadDump: Failed to add the foreign key constraint. Missing column 'film_id' for constraint");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> no dryRun to get errors from mismatched definitions {VER(<8.0.0)}
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {ignoreExistingObjects: true});}, "Util.loadDump: Cannot add foreign key constraint");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> threads:1
// TSFR10_1
util.loadDump(__tmp_dir+"/ldtest/dump", {threads: 1});
EXPECT_OUTPUT_CONTAINS(" using 1 thread.");

// compare loaded dump except for accounts list
EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> showProgress:true
// TSFR11_1
testutil.callMysqlsh([__sandbox_uri1, "--", "util", "load-dump", __tmp_dir+"/ldtest/dump", "--showProgress=true"]);

EXPECT_STDOUT_CONTAINS("thds loading");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> showProgress:false
// TSFR11_2
testutil.callMysqlsh([__sandbox_uri1, "--", "util", "load-dump", __tmp_dir+"/ldtest/dump", "--showProgress=false"]);

EXPECT_STDOUT_NOT_CONTAINS("thds loading");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> dryRun:true
// TSFR9_1
var before=snapshot_instance(session);

util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: true});

EXPECT_NO_CHANGES(session, before);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> dryRun:false
// TSFR9_2
var before=snapshot_instance(session);

util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: false});

EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> defaultCharacterSet:binary
// TSFR14_1, TSFR14_3
util.loadDump(__tmp_dir+"/ldtest/dump", {defaultCharacterSet: "binary"});

EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> defaultCharacterSet:invalid (should fail)
// TSFR14_5
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {defaultCharacterSet: "bog\"`'us"})}, "Util.loadDump: Unknown character set: 'bog\"`'us'");
EXPECT_OUTPUT_CONTAINS("ERROR: [Worker001] Error opening connection to MySQL: MySQL Error 1115 (42000): Unknown character set: 'bog\"`'us'");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

// Various special cases
// ---------------------

//@<> check abort when a worker has a fatal error at connect time

session.runSql("set global max_connections=3");

// This will throw either Too many connections or Aborted
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {threads: 8});}, "Util.loadDump: ");

EXPECT_OUTPUT_CONTAINS("] Error opening connection to MySQL: MySQL Error 1040 (HY000): Too many connections");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

session.runSql("set global max_connections=1000");

//@<> check abort when a worker has a fatal error during load

// corrupt one of the data files to force the load to fail
testutil.cpfile(__tmp_dir+"/ldtest/dump-nochunk/xtest@t_json.tsv.zstd", __tmp_dir+"/ldtest/dump-nochunk/xtest@t_json.tsv.zstd.bak");
testutil.createFile(__tmp_dir+"/ldtest/dump-nochunk/xtest@t_json.tsv.zstd", "badfile");

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump-nochunk");}, "Util.loadDump: Error loading dump");

EXPECT_OUTPUT_CONTAINS("xtest@t_json.tsv.zstd: zstd.read: Unknown frame descriptor");
EXPECT_OUTPUT_CONTAINS("ERROR: Aborting load...");

testutil.rename(__tmp_dir+"/ldtest/dump-nochunk/xtest@t_json.tsv.zstd.bak", __tmp_dir+"/ldtest/dump-nochunk/xtest@t_json.tsv.zstd");
testutil.rmfile(__tmp_dir+"/ldtest/dump-nochunk/load-progress*");
wipe_instance(session);

//@<> try to load using an account with missing privs
session.runSql("create user user@localhost");
session.runSql("grant all on sakila.* to user@localhost with grant option");
session.runSql("grant select,insert,update on xtest.* to user@localhost with grant option");
session.runSql("grant select on test.* to user@localhost with grant option");
session.runSql("grant create,insert on mysqlaas_compat.* to user@localhost with grant option");

shell.connect("mysql://user@localhost:"+__mysql_sandbox_port1);

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump-nochunk");}, "Util.loadDump: Access denied for user 'user'@'localhost' to database");

shell.connect(__sandbox_uri1);
testutil.rmfile(__tmp_dir+"/ldtest/dump-nochunk/load-progress*");
wipe_instance(session);

// Progress File
// -------------

// change path of progressFile already tested

//@<> disable progress file
// resuming should be auto-disabled
util.loadDump(__tmp_dir+"/ldtest/dump", {progressFile: ""});

EXPECT_FILE_NOT_EXISTS(__tmp_dir+"/ldtest/dump/load-progress."+uuid+".json");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> invalid path for progress file

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {progressFile: "/bad/path"});}, "Util.loadDump: Cannot open file '/bad/path': No such file or directory");


//@<> skipBinlog:true
// TSFR15_1, TSFR15_3, TSFR15_4
old_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];

util.loadDump(__tmp_dir+"/ldtest/dump", {skipBinlog: true});

new_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];

EXPECT_EQ(old_gtid_executed, new_gtid_executed);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);


// Resuming
// --------

//@<> TSFR12_2 - prepare
// first load the dump
util.loadDump(__tmp_dir+"/ldtest/dump");

//@<> load again using a different progress file should assume a fresh load
// in practice this means the load will fail because of duplicate objects from the previous attempt
// TSFR12_2
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {progressFile: __tmp_dir+"/progress", dryRun: 1});}, "Util.loadDump: Duplicate objects found in destination database");

EXPECT_STDOUT_NOT_CONTAINS("Load progress file detected.");

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
testutil.rmfile(__tmp_dir+"/progress");
wipe_instance(session);

//@<> resume + dryRun without a progress file
var before=snapshot_instance(session);
old_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];
// TSFR12_1
util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: true});

// DB should be untouched and no files should've been created because of dryRun
EXPECT_FILE_NOT_EXISTS(__tmp_dir+"/ldtest/dump/load-progress."+uuid+".json");

new_gtid_executed=session.runSql("SELECT concat(@@global.gtid_executed,':',@@global.gtid_purged)").fetchOne()[0];
EXPECT_EQ(old_gtid_executed, new_gtid_executed);

EXPECT_NO_CHANGES(session, before);

//@<> load data partially (during DDL execution)

// force an error in the middle by corrupting one of the files
testutil.cpfile(__tmp_dir+"/ldtest/dump/xtest@t_json.sql", __tmp_dir+"/ldtest/dump/xtest@t_json.sql.bak");
testutil.createFile(__tmp_dir+"/ldtest/dump/xtest@t_json.sql", "call invalid()\n");

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Util.loadDump: PROCEDURE xtest.invalid does not exist");

// Get list of tables that were already loaded
var entries=load_progress(__tmp_dir+"/ldtest/dump/load-progress."+uuid+".json");
var done_tables=[];
for(i in entries) {
  entry=entries[i];
  if(entry.op=="TABLE-DDL"&&entry.done) {
    done_tables.push("`"+entry.schema+"`.`"+entry.table+"`");
  }
}

EXPECT_OUTPUT_CONTAINS("Executing DDL script for `xtest`.`t_json`");
for(t in done_tables) {
  table=done_tables[t];
  EXPECT_OUTPUT_CONTAINS("Executing DDL script for "+table);
}
EXPECT_OUTPUT_NOT_CONTAINS("[Worker");

// restore corrupted file
testutil.cpfile(__tmp_dir+"/ldtest/dump/xtest@t_json.sql.bak", __tmp_dir+"/ldtest/dump/xtest@t_json.sql");

//@<> resume should re-execute the failed DDL now
// TSFR12_4
util.loadDump(__tmp_dir+"/ldtest/dump");

// this should have succeeded
for(t in done_tables) {
  table=done_tables[t];
  EXPECT_OUTPUT_NOT_CONTAINS("Executing DDL script for "+table);
  EXPECT_OUTPUT_NOT_CONTAINS("Re-executing DDL script for "+table);
}

// this is where it failed
EXPECT_OUTPUT_CONTAINS("Re-executing DDL script for `xtest`.`t_json`");

EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session);

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> load data partially (during data load)

// force an error in the middle by corrupting one of the files
testutil.cpfile(__tmp_dir+"/ldtest/dump-big/test@primer-dataset-id@130.tsv.zstd", __tmp_dir+"/ldtest/dump-big/test@primer-dataset-id@130.tsv.zstd.bak");
testutil.createFile(__tmp_dir+"/ldtest/dump-big/test@primer-dataset-id@130.tsv.zstd", "badfile");

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump-big");}, "Error loading dump");

EXPECT_OUTPUT_CONTAINS("Executing DDL script for ");

// restore corrupted file
testutil.cpfile(__tmp_dir+"/ldtest/dump-big/test@primer-dataset-id@130.tsv.zstd.bak", __tmp_dir+"/ldtest/dump-big/test@primer-dataset-id@130.tsv.zstd");

//@<> resume partial load of data
// TSFR12_3
// DDL shouldn't be re-executed but sakila.film should be loaded
testutil.callMysqlsh([__sandbox_uri1, "--", "util", "load-dump", __tmp_dir+"/ldtest/dump-big", "--showProgress=true"]);

EXPECT_STDOUT_NOT_CONTAINS("Executing DDL script for ");
EXPECT_STDOUT_CONTAINS("Load progress file detected. Load will be resumed from where it was left, assuming no external updates were made.");
// also check that progress didn't restart from 0, since some data was already loaded
EXPECT_STDOUT_CONTAINS(" 100% ");
EXPECT_STDOUT_CONTAINS(" thds loading");

// TSFR12_5 - ensures everything including data was loaded completely
EXPECT_DUMP_LOADED_IGNORE_ACCOUNTS(session, reference_big);

//@<> check that the default is to resume
// loading again on a loaded DB should fail b/c of duplicate objects
util.loadDump(__tmp_dir+"/ldtest/dump-big");

EXPECT_OUTPUT_CONTAINS("NOTE: Load progress file detected. Load will be resumed from where it was left, assuming no external updates were made");

//@<> try loading an already loaded dump
util.loadDump(__tmp_dir+"/ldtest/dump-big");

//@<> try loading an already loaded dump after resetting progress (will fail because of duplicate objects)
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump-big", {resetProgress: 1});}, "Util.loadDump: Duplicate objects found in destination database");

EXPECT_OUTPUT_NOT_CONTAINS("test@primer-dataset-id@1.tsv.zstd: Records:");
EXPECT_OUTPUT_CONTAINS("ERROR: Schema `sakila` already contains a view named ");

//@<> try again after wiping out the server
wipe_instance(session);

util.loadDump(__tmp_dir+"/ldtest/dump-big", {resetProgress: 1});

EXPECT_OUTPUT_CONTAINS("test@primer-dataset-id@1.tsv.zstd: Records:");
EXPECT_OUTPUT_CONTAINS("Executing DDL script for ");

// cleanup
testutil.rmfile(__tmp_dir+"/ldtest/dump-big/load-progress*");
wipe_instance(session);


// Bad dumps
// ---------

//@<> dump version is newer than supported
data=JSON.parse(os.loadTextFile(__tmp_dir+"/ldtest/dump/@.json"));
testutil.rename(__tmp_dir+"/ldtest/dump/@.json", __tmp_dir+"/ldtest/dump/@.json.bak");
data["version"]="2.0.0";
testutil.createFile(__tmp_dir+"/ldtest/dump/@.json", JSON.stringify(data));

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Util.loadDump: Unsupported dump version");

data["version"]="1.1.0";
testutil.createFile(__tmp_dir+"/ldtest/dump/@.json", JSON.stringify(data));

EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Util.loadDump: Unsupported dump version");

data["version"]="1.0.1";
testutil.createFile(__tmp_dir+"/ldtest/dump/@.json", JSON.stringify(data));

util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1});


testutil.rename(__tmp_dir+"/ldtest/dump/@.json.bak", __tmp_dir+"/ldtest/dump/@.json");

//@<> Unfinished dump should fail

// check abort if @.done.json is missing and waiting not enabled
// TSFR3_1
testutil.rename(__tmp_dir+"/ldtest/dump/@.done.json", __tmp_dir+"/ldtest/dump/@.done.json.bak");
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump");}, "Incomplete dump");
EXPECT_OUTPUT_CONTAINS("ERROR: Dump is not yet finished. Use the 'waitDumpTimeout' option to enable concurrent load and set a timeout for when we need to wait for new data to become available.");

//@<> Unfinished dump with timeout (fail)
// TSFR3_2
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump", {waitDumpTimeout: 2});}, "Dump timeout");
EXPECT_OUTPUT_CONTAINS("Dump is still ongoing, data will be loaded as it becomes available.");
EXPECT_OUTPUT_CONTAINS("Waiting for more data to become available...");
EXPECT_OUTPUT_CONTAINS("WARNING: Timeout while waiting for dump to finish. Imported data may be incomplete.");

//@<> Unfinished dump with timeout (success)
// rename the done file after 2s
var p=testutil.callMysqlshAsync(["--py", "-e", 'import time;time.sleep(5);import os;os.chdir("'+filename_for_file(__tmp_dir)+'/ldtest/dump");os.rename("@.done.json.bak", "@.done.json")']);

util.loadDump(__tmp_dir+"/ldtest/dump", {waitDumpTimeout: 10});
EXPECT_OUTPUT_CONTAINS("Dump is still ongoing, data will be loaded as it becomes available.");
EXPECT_OUTPUT_CONTAINS("Waiting for more data to become available...");
EXPECT_OUTPUT_NOT_CONTAINS("Timeout while waiting");

testutil.waitMysqlshAsync(p);

//@<> Create a dump with histogram in some tables
session.runSql("ANALYZE TABLE sakila.film UPDATE HISTOGRAM ON film_id, title");
session.runSql("ANALYZE TABLE sakila.rental UPDATE HISTOGRAM ON customer_id WITH 7 BUCKETS");
session.runSql("ANALYZE TABLE sakila.payment UPDATE HISTOGRAM ON rental_id, customer_id, staff_id WITH 2 BUCKETS");
util.dumpSchemas(["sakila"], __tmp_dir+"/ldtest/dump-sakila");
wipe_instance(session);

//@<> Load everything with no analyze
util.loadDump(__tmp_dir+"/ldtest/dump-sakila", {analyzeTables: "off"});

//@<> Analyze only
util.loadDump(__tmp_dir+"/ldtest/dump-sakila", {analyzeTables: "on", loadData: false, loadDdl: false});

EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`store`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`staff`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`language`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`category`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`country`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`inventory`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`film_text`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`customer`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`film_category`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`city`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`film_actor`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`actor`");
EXPECT_OUTPUT_CONTAINS("Analyzing table `sakila`.`address`");

if(__version_num>80000) {
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`payment`");
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`rental`");
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`film`");
}

//@<> Load everything and analyze only for histograms
wipe_instance(session);
util.loadDump(__tmp_dir+"/ldtest/dump-sakila", {analyzeTables: "histogram"});
if(__version_num>80000) {
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`film`");
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`rental`");
  EXPECT_OUTPUT_CONTAINS("Updating histogram for table `sakila`.`payment`");
} else {
  EXPECT_OUTPUT_CONTAINS("WARNING: Histogram creation enabled but MySQL Server "+__version+" does not support it.");
}

//@<> Ensure tables with no PK are truncated before reloading during a resume
session.runSql("set global local_infile=1");
session.runSql("create schema test");
session.runSql("create table test.nopk (a int, b json)");
session.runSql("create table test.pk (a int primary key, b json)");
session.runSql("create table test.uk (a int unique not null, b json)");
session.runSql("create table test.ukn (a int unique null, b json)");
util.dumpInstance(__tmp_dir+"/ldtest/dump-nopktest", {chunking: 0, compression: "none", compatibility: ['strip_tablespaces']});

session.runSql("insert into test.nopk values (0, '{}')")
session.runSql("insert into test.pk values (0, '{}')")
session.runSql("insert into test.uk values (0, '{}')")
session.runSql("insert into test.ukn values (0, '{}')")

// mess up the data files so that the load fails
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@nopk.tsv", "1\taaaa\n2\t\1\2\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@pk.tsv", "1\taaaa\n2\t\1\2\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@uk.tsv", "1\taaaa\n2\t\1\2\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@ukn.tsv", "1\taaaa\n2\t\1\2\n");
// loading should fail for all tables
EXPECT_THROWS(function () {util.loadDump(__tmp_dir+"/ldtest/dump-nopktest", {ignoreExistingObjects: true});}, "Util.loadDump: Error loading dump");

// check that the progress file marked all tables are failed
check_load_progress_all_data_failed(__tmp_dir+"/ldtest/dump-nopktest/load-progress."+uuid+".json");

// try again, resuming with good data
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@nopk.tsv", "1\t{}\n2\t{}\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@pk.tsv", "1\t{}\n2\t{}\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@uk.tsv", "1\t{}\n2\t{}\n");
testutil.createFile(__tmp_dir+"/ldtest/dump-nopktest/test@ukn.tsv", "1\t{}\n2\t{}\n");
util.loadDump(__tmp_dir+"/ldtest/dump-nopktest", {ignoreExistingObjects: true});

// this should have been truncated before the load
r=shell.dumpRows(session.runSql("select * from test.nopk"));
EXPECT_EQ(2, r);

// this should not be truncated
r=shell.dumpRows(session.runSql("select * from test.pk"));
EXPECT_EQ(3, r);

// this should not be truncated
r=shell.dumpRows(session.runSql("select * from test.uk"));
EXPECT_EQ(3, r);

// this should be truncated
r=shell.dumpRows(session.runSql("select * from test.ukn"));
EXPECT_EQ(2, r);

//@<> Check work distribution across threads
// Tables should finish loading at about the same time regardless of the 
// # of chunks

// fill test data
wipe_instance(session);

session.runSql("create schema test");
session.runSql("use test");
// create a dump with 4 tables
ntables=4;
for(i=0; i<ntables; i++) {
  name="table"+i;
  session.runSql("create table "+name+" (id int primary key, value text)");
}
util.dumpInstance(__tmp_dir+"/ldtest/dump2", {compression: "none", compatibility: ['strip_tablespaces']});
// add extra artificial chunks for the tables, with more chunks in some of them
for(i=0; i<ntables; i++) {
  name="table"+i;
  testutil.rmfile(__tmp_dir+"/ldtest/dump2/test@"+name+"@@0.tsv");
  for(c=0; c<10+i*20; c++) {
    data="";
    for(j=0; j<1000; j++)
      data+=(c*1000+j)+"\txxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n";
    testutil.createFile(__tmp_dir+"/ldtest/dump2/test@"+name+"@"+c+".tsv", data);
  }
  testutil.createFile(__tmp_dir+"/ldtest/dump2/test@"+name+"@@"+c+".tsv", (c*1000+j)+"\txxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
}

testutil.rmfile(__tmp_dir+"/ldtest/dump2/load-progress*");
wipe_instance(session);

//@<> more tables than threads
util.loadDump(__tmp_dir+"/ldtest/dump2", {threads: 2});
var uuid=session.runSql("select @@server_uuid").fetchOne()[0];
check_worker_distribution(2, ntables, __tmp_dir+"/ldtest/dump2/load-progress."+uuid+".json");

testutil.rmfile(__tmp_dir+"/ldtest/dump2/load-progress*");
wipe_instance(session);

//@<> more threads than tables
util.loadDump(__tmp_dir+"/ldtest/dump2", {threads: 16});

check_worker_distribution(16, ntables, __tmp_dir+"/ldtest/dump2/load-progress."+uuid+".json");

testutil.rmfile(__tmp_dir+"/ldtest/dump2/load-progress*");
wipe_instance(session);

//@<> Load dump with GR running

testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

cluster=dba.createCluster("cluster");

// xtest has tables without PK, so we skip it
util.loadDump(__tmp_dir+"/ldtest/dump", {excludeSchemas: ["xtest"]});

var noxtest=JSON.parse(JSON.stringify(reference));
delete noxtest["schemas"]["xtest"];

var stripped=snapshot_instance(session);
delete stripped["schemas"]["mysql_innodb_cluster_metadata"];

delete noxtest["accounts"];
delete stripped["accounts"];

EXPECT_JSON_EQ(noxtest, stripped);

session.runSql("stop group_replication");
session.runSql("set global super_read_only=0");
testutil.rmfile(__tmp_dir+"/ldtest/dump/load-progress*");
wipe_instance(session);

//@<> Try to load via xproto (should auto-switch to classic and work)

shell.connect("mysqlx://root:root@127.0.0.1:"+__mysql_sandbox_x_port1);

util.loadDump(__tmp_dir+"/ldtest/dump", {dryRun: 1});

shell.connect(__sandbox_uri1);

//@<> Cleanup
testutil.destroySandbox(__mysql_sandbox_port1);
testutil.rmdir(__tmp_dir+"/ldtest", true);