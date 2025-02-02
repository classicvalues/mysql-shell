# imports
import json
import os
import os.path
import re
import shutil
import stat
import time
import urllib.parse

# constants
world_x_schema = "world_x_cities"
world_x_table = "cities"

# schema name can consist of 51 reserved characters max, server supports up to
# 64 chars but when it creates the directory for schema, it encodes non-letters
# ASCII using five bytes, meaning the directory name is going to be 255
# characters long (max on most # platforms)
# schema name consists of 49 reserved characters + UTF-8 character
test_schema = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ó"
test_table_primary = "first"
test_table_unique = "second"
# mysql_data_home + schema + table name + path separators cannot exceed 512
# characters
# table name consists of 48 reserved characters + UTF-8 character
test_table_non_unique = "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ą"
test_table_no_index = "fóurth"
test_table_trigger = "sample_trigger"
test_schema_procedure = "sample_procedure"
test_schema_function = "sample_function"
test_schema_event = "sample_event"
test_view = "sample_view"
test_user = "sample_user"
test_user_pwd = "p4$$"
test_privilege = "FILE"

verification_schema = "wl13807_ver"

types_schema = "xtest"

incompatible_schema = "mysqlaas"
incompatible_table_wrong_engine = "wrong_engine"
incompatible_table_encryption = "has_encryption"
incompatible_table_data_directory = "has_data_dir"
incompatible_table_index_directory = "has_index_dir"
incompatible_tablespace = "sample_tablespace"
incompatible_table_tablespace = "use_tablespace"
incompatible_view = "has_definer"

incompatible_table_directory = os.path.join(__tmp_dir, "incompatible")
table_data_directory = os.path.join(incompatible_table_directory, "data")
table_index_directory = os.path.join(incompatible_table_directory, "index")

shutil.rmtree(incompatible_table_directory, True)
os.makedirs(incompatible_table_directory)
os.mkdir(table_data_directory)
os.mkdir(table_index_directory)

if __os_type == "windows":
    # on windows server would have to be installed in the root folder to
    # handle long schema/table names
    if __version_num >= 80000:
        test_schema = "@ó"
        test_table_non_unique = "@ą"
    else:
        # when using latin1 encoding, UTF-8 characters sent by shell are converted by server
        # to latin1 representation, then upper case characters are converted to lower case,
        # which leads to invalid UTF-8 sequences when names are transfered back to shell
        # use ASCII in this case
        test_schema = "@o"
        test_table_non_unique = "@a"
        test_table_no_index = "fourth"

all_schemas = [world_x_schema, types_schema, test_schema, verification_schema, incompatible_schema]

uri = __sandbox_uri1
xuri = __sandbox_uri1.replace("mysql://", "mysqlx://") + "0"

test_output_relative = "dump_output"
test_output_absolute = os.path.abspath(test_output_relative)

# helpers
if __os_type != "windows":
    def filename_for_file(filename):
        return filename
else:
    def filename_for_file(filename):
        return filename.replace("\\", "/")

if __os_type != "windows":
    def absolute_path_for_output(path):
        return path
else:
    def absolute_path_for_output(path):
        long_path_prefix = r"\\?" "\\"
        return long_path_prefix + path

def setup_session(u = uri):
    shell.connect(u)
    session.run_sql("SET NAMES 'utf8mb4';")
    session.run_sql("SET GLOBAL local_infile = true;")

def drop_all_schemas(exclude=[]):
    for schema in session.run_sql("SELECT SCHEMA_NAME FROM information_schema.schemata;").fetch_all():
        if schema[0] not in ["information_schema", "mysql", "performance_schema", "sys"] + exclude:
            session.run_sql("DROP SCHEMA IF EXISTS !;", [ schema[0] ])

def create_all_schemas():
    for schema in all_schemas:
        session.run_sql("CREATE SCHEMA !;", [ schema ])

def create_user():
    session.run_sql("DROP USER IF EXISTS !@!;", [test_user, __host])
    session.run_sql("CREATE USER IF NOT EXISTS !@! IDENTIFIED BY ?;", [test_user, __host, test_user_pwd])
    session.run_sql("GRANT {0} ON *.* TO !@!;".format(test_privilege), [test_user, __host])

def recreate_verification_schema():
    session.run_sql("DROP SCHEMA IF EXISTS !;", [ verification_schema ])
    session.run_sql("CREATE SCHEMA !;", [ verification_schema ])

def urlencode(s):
    ret = ""
    for c in s:
        if ord(c) <= 127:
            ret += urllib.parse.quote(c)
        else:
            ret += c
    return ret

def truncate_basename(basename):
    if len(basename) > 225:
        # there are no names which would create the same truncated basename,
        # always append "0" as ordinal number
        return basename[:225] + "0"
    else:
        return basename

# WL13807-FR8 - The base name of any schema-related file created during the dump must be in the form of `schema`, where:
# * schema - percent-encoded name of the schema.
# Only code points up to and including `U+007F` which are not Unreserved Characters (as specified in RFC3986) must be encoded, all remaining code points must not be encoded. If the length of base name exceeds `225` characters, it must be truncated to `225` characters and an ordinal number must be appended.
# WL13807-TSFR8_1
# WL13807-TSFR8_2
def encode_schema_basename(schema):
    return truncate_basename(urlencode(schema))

# WL13807-FR7.1 - The base name of any file created during the dump must be in the format `schema@table`, where:
# * `schema` - percent-encoded name of the schema which contains the table to be exported,
# * `table` - percent-encoded name of the table to be exported.
# Only code points up to and including `U+007F` which are not Unreserved Characters (as specified in RFC3986) must be encoded, all remaining code points must not be encoded. If the length of base name exceeds `225` characters, it must be truncated to `225` characters and an ordinal number must be appended.

def encode_table_basename(schema, table):
    return truncate_basename(urlencode(schema) + "@" + urlencode(table))

def count_files_with_basename(directory, basename):
    cnt = 0
    for f in os.listdir(directory):
        if f.startswith(basename):
            cnt += 1
    return cnt

def has_file_with_basename(directory, basename):
    return count_files_with_basename(directory, basename) > 0

def count_files_with_extension(directory, ext):
    cnt = 0
    for f in os.listdir(directory):
        if f.endswith(ext):
            cnt += 1
    return cnt

def EXPECT_SUCCESS(schemas, outputUrl, options = {}):
    WIPE_STDOUT()
    shutil.rmtree(test_output_absolute, True)
    EXPECT_FALSE(os.path.isdir(test_output_absolute))
    util.dump_schemas(schemas, outputUrl, options)
    EXPECT_TRUE(os.path.isdir(test_output_absolute))
    EXPECT_STDOUT_CONTAINS("Schemas dumped: {0}".format(len(schemas)))

def EXPECT_FAIL(error, msg, schemas, outputUrl, options = {}, expect_dir_created = False):
    shutil.rmtree(test_output_absolute, True)
    full_msg = "{0}: Util.dump_schemas: {1}".format(error, msg.pattern if is_re_instance(msg) else msg)
    if is_re_instance(msg):
        full_msg = re.compile("^" + full_msg)
    EXPECT_THROWS(lambda: util.dump_schemas(schemas, outputUrl, options), full_msg)
    EXPECT_EQ(expect_dir_created, os.path.isdir(test_output_absolute))

def TEST_BOOL_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Bool, but is Null".format(option), [types_schema], test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' Bool expected, but value is String".format(option), [types_schema], test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Bool, but is Array".format(option), [types_schema], test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Bool, but is Map".format(option), [types_schema], test_output_relative, { option: {} })

def TEST_STRING_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Null".format(option), [types_schema], test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Integer".format(option), [types_schema], test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Integer".format(option), [types_schema], test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Array".format(option), [types_schema], test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Map".format(option), [types_schema], test_output_relative, { option: {} })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type String, but is Bool".format(option), [types_schema], test_output_relative, { option: False })

def TEST_UINT_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type UInteger, but is Null".format(option), [types_schema], test_output_relative, { option: None })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' UInteger expected, but Integer value is out of range".format(option), [types_schema], test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' UInteger expected, but value is String".format(option), [types_schema], test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type UInteger, but is Array".format(option), [types_schema], test_output_relative, { option: [] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type UInteger, but is Map".format(option), [types_schema], test_output_relative, { option: {} })

def TEST_ARRAY_OF_STRINGS_OPTION(option):
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Array, but is Integer".format(option), [types_schema], test_output_relative, { option: 5 })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Array, but is Integer".format(option), [types_schema], test_output_relative, { option: -5 })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Array, but is String".format(option), [types_schema], test_output_relative, { option: "dummy" })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Array, but is Map".format(option), [types_schema], test_output_relative, { option: {} })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' is expected to be of type Array, but is Bool".format(option), [types_schema], test_output_relative, { option: False })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' String expected, but value is Null".format(option), [types_schema], test_output_relative, { option: [ None ] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' String expected, but value is Integer".format(option), [types_schema], test_output_relative, { option: [ 5 ] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' String expected, but value is Integer".format(option), [types_schema], test_output_relative, { option: [ -5 ] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' String expected, but value is Map".format(option), [types_schema], test_output_relative, { option: [ {} ] })
    EXPECT_FAIL("TypeError", "Argument #3: Option '{0}' String expected, but value is Bool".format(option), [types_schema], test_output_relative, { option: [ False ] })

def get_load_columns(options):
    decode = options["decodeColumns"] if "decodeColumns" in options else {}
    columns = []
    variables = []
    for c in options["columns"]:
        if c in decode:
            var = "@var{0}".format(len(variables))
            variables.append("{0} = {1}({2})".format(c, decode[c], var))
            columns.append(var)
        else:
            columns.append(c)
    statement = "(" + ",".join(columns) + ")"
    if variables:
        statement += " SET " + ",".join(variables)
    return statement

def get_all_columns(schema, table):
    columns = []
    for column in session.run_sql("SELECT COLUMN_NAME FROM information_schema.columns WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? ORDER BY ORDINAL_POSITION;", [schema, table]).fetch_all():
        columns.append(column[0])
    return columns

def compute_crc(schema, table, columns):
    session.run_sql("SET @crc = '';")
    session.run_sql("SELECT @crc := MD5(CONCAT_WS('#',@crc,{0})) FROM !.! ORDER BY {0};".format(("!," * len(columns))[:-1]), columns + [schema, table] + columns)
    return session.run_sql("SELECT @crc;").fetch_one()[0]

def TEST_LOAD(schema, table, chunked):
    print("---> testing: `{0}`.`{1}`".format(schema, table))
    # only uncompressed files are supported
    basename = encode_table_basename(schema, table)
    # read global options
    with open(os.path.join(test_output_absolute, "@.json"), encoding="utf-8") as json_file:
        tz_utc = json.load(json_file)["tzUtc"]
    # read options from metadata file
    with open(os.path.join(test_output_absolute, basename + ".json"), encoding="utf-8") as json_file:
        options = json.load(json_file)["options"]
    columns_statement = get_load_columns(options)
    local_infile_statement = """'
    INTO TABLE `{0}`.`{1}`
    CHARACTER SET '{2}'
    FIELDS TERMINATED BY {3} {4}ENCLOSED BY {5} ESCAPED BY {6}
    LINES TERMINATED BY {7}
    {8};\n""".format(verification_schema, table, options["defaultCharacterSet"], repr(options["fieldsTerminatedBy"]), "OPTIONALLY " if options["fieldsOptionallyEnclosed"] else "", repr(options["fieldsEnclosedBy"]), repr(options["fieldsEscapedBy"]), repr(options["linesTerminatedBy"]), columns_statement)
    # get files with data
    data_files = []
    for f in os.listdir(test_output_absolute):
        if chunked:
            if f.startswith(basename + "@") and f.endswith(".tsv"):
                data_files.append(f)
        else:
            if f == basename + ".tsv":
                data_files.append(f)
    # on Windows file path in LOAD DATA is passed directly to the function which opens the file
    # (without any encoding/decoding), rename them to avoid problems
    if __os_type == "windows":
        backup_data_files = {}
        for idx, old_name in enumerate(data_files):
            new_name = old_name.encode("ascii", "replace").decode().replace("?", "_")
            os.rename(os.path.join(test_output_absolute, old_name), os.path.join(test_output_absolute, new_name))
            backup_data_files[new_name] = old_name
            data_files[idx] = new_name
    # write SQL file which will load everything
    sql_file = os.path.join(test_output_absolute, "data_" + basename + ".sql")
    with open(sql_file, "w", encoding="utf-8") as f:
        f.write("SET NAMES '{0}';\n".format(options["defaultCharacterSet"]))
        f.write("SET SQL_MODE='';\n")
        if tz_utc:
            f.write("SET TIME_ZONE = '+00:00';\n")
        f.write("source {0} ;\n".format(filename_for_file(os.path.join(test_output_absolute, basename + ".sql"))))
        for data in data_files:
            f.write("LOAD DATA LOCAL INFILE '")
            f.write(filename_for_file(os.path.join(test_output_absolute, data)))
            f.write(local_infile_statement)
    # load data
    recreate_verification_schema()
    EXPECT_EQ(0, testutil.call_mysqlsh([uri + "/" + verification_schema + "?local-infile=true", "--sql", "--file", sql_file]))
    # rename back the data files
    if __os_type == "windows":
        for new_name, old_name in backup_data_files.items():
            os.rename(os.path.join(test_output_absolute, new_name), os.path.join(test_output_absolute, old_name))
    #compute CRC
    all_columns = get_all_columns(schema, table)
    EXPECT_EQ(compute_crc(schema, table, all_columns), compute_crc(verification_schema, table, all_columns))

#@<> WL13807-FR2.4 - an exception must be thrown if there is no global session
EXPECT_FAIL("RuntimeError", "An open session is required to perform this operation.", ['mysql'], test_output_relative)

#@<> deploy sandbox
testutil.deploy_raw_sandbox(__mysql_sandbox_port1, "root", {
    "loose_innodb_directories": filename_for_file(table_data_directory),
    "early-plugin-load": "keyring_file." + ("dll" if __os_type == "windows" else "so"),
    "keyring_file_data": filename_for_file(os.path.join(incompatible_table_directory, "keyring"))
})

#@<> wait for server
testutil.wait_sandbox_alive(uri)
shell.connect(uri)

#@<> WL13807-FR2.4 - an exception must be thrown if there is no open global session
session.close()
EXPECT_FAIL("RuntimeError", "An open session is required to perform this operation.", ['mysql'], test_output_relative)

#@<> Setup
setup_session()

drop_all_schemas()
create_all_schemas()

create_user()

session.run_sql("CREATE TABLE !.! (`ID` int(11) NOT NULL AUTO_INCREMENT, `Name` char(64) NOT NULL DEFAULT '', `CountryCode` char(3) NOT NULL DEFAULT '', `District` char(64) NOT NULL DEFAULT '', `Info` json DEFAULT NULL, PRIMARY KEY (`ID`)) ENGINE=InnoDB AUTO_INCREMENT=4080 DEFAULT CHARSET=utf8mb4;", [ world_x_schema, world_x_table ])
util.import_table(os.path.join(__import_data_path, "world_x_cities.dump"), { "schema": world_x_schema, "table": world_x_table, "characterSet": "utf8mb4", "showProgress": False })

rc = testutil.call_mysqlsh([uri, "--sql", "--file", os.path.join(__data_path, "sql", "fieldtypes_all.sql")])
EXPECT_EQ(0, rc)

# table with primary key, unique key, generated columns
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL AUTO_INCREMENT PRIMARY KEY, `data` INT NOT NULL UNIQUE, `vdata` INT GENERATED ALWAYS AS (data + 1) VIRTUAL, `sdata` INT GENERATED ALWAYS AS (data + 2) STORED) ENGINE=InnoDB;", [ test_schema, test_table_primary ])
# table with unique key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT NOT NULL UNIQUE, `data` INT) ENGINE=InnoDB;", [ test_schema, test_table_unique ])
# table with non-unique key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT, KEY (`id`)) ENGINE=InnoDB;", [ test_schema, test_table_non_unique ])
# table without key
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT, `tdata` INT) ENGINE=InnoDB;", [ test_schema, test_table_no_index ])

session.run_sql("CREATE VIEW !.! AS SELECT `tdata` FROM !.!;", [ test_schema, test_view, test_schema, test_table_no_index ])

session.run_sql("CREATE TRIGGER !.! BEFORE UPDATE ON ! FOR EACH ROW BEGIN SET NEW.tdata = NEW.data + 3; END;", [ test_schema, test_table_trigger, test_table_no_index ])

session.run_sql("""
CREATE PROCEDURE !.!()
BEGIN
  DECLARE i INT DEFAULT 1;

  WHILE i < 10000 DO
    INSERT INTO !.! (`data`) VALUES (i);
    SET i = i + 1;
  END WHILE;
END""", [ test_schema, test_schema_procedure, test_schema, test_table_primary ])

session.run_sql("CREATE FUNCTION !.!(s CHAR(20)) RETURNS CHAR(50) DETERMINISTIC RETURN CONCAT('Hello, ',s,'!');", [ test_schema, test_schema_function ])

session.run_sql("CREATE EVENT !.! ON SCHEDULE EVERY 1 HOUR STARTS CURRENT_TIMESTAMP + INTERVAL 1 WEEK DO DELETE FROM !.!;", [ test_schema, test_schema_event, test_schema, test_table_no_index ])

session.run_sql("""INSERT INTO !.! (`data`)
SELECT (tth * 10000 + th * 1000 + h * 100 + t * 10 + u + 1) x FROM
    (SELECT 0 tth UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) E,
    (SELECT 0 th UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) D,
    (SELECT 0 h UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) C,
    (SELECT 0 t UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) B,
    (SELECT 0 u UNION SELECT 1 UNION SELECT 2 UNION SELECT 3 UNION SELECT 4 UNION SELECT 5 UNION SELECT 6 UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) A
ORDER BY x;""", [ test_schema, test_table_primary ])

session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_unique, test_schema, test_table_primary ])
session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_non_unique, test_schema, test_table_primary ])
# duplicate row, NULL row
session.run_sql("INSERT INTO !.! (`id`, `data`) VALUES (1, 1), (NULL, NULL);", [ test_schema, test_table_non_unique ])
session.run_sql("INSERT INTO !.! (`id`, `data`) SELECT `id`, `data` FROM !.!;", [ test_schema, test_table_no_index, test_schema, test_table_primary ])
# duplicate row, NULL row
session.run_sql("INSERT INTO !.! (`id`, `data`) VALUES (1, 1), (NULL, NULL);", [ test_schema, test_table_no_index ])

for table in [test_table_primary, test_table_unique, test_table_non_unique, test_table_no_index]:
    session.run_sql("ANALYZE TABLE !.!;", [ test_schema, table ])

#@<> schema with MySQLaaS incompatibilities
session.run_sql("ALTER DATABASE ! CHARACTER SET latin1;", [ incompatible_schema ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=MyISAM DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_wrong_engine ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=InnoDB ENCRYPTION = 'Y' DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_encryption ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=InnoDB DATA DIRECTORY = '{0}' DEFAULT CHARSET=latin1;".format(filename_for_file(table_data_directory)), [ incompatible_schema, incompatible_table_data_directory ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) ENGINE=MyISAM INDEX DIRECTORY = '{0}' DEFAULT CHARSET=latin1;".format(filename_for_file(table_index_directory)), [ incompatible_schema, incompatible_table_index_directory ])
session.run_sql("CREATE TABLESPACE ! ADD DATAFILE 't_s_1.ibd' ENGINE=INNODB;", [ incompatible_tablespace ])
session.run_sql("CREATE TABLE !.! (`id` MEDIUMINT, `data` INT) TABLESPACE ! DEFAULT CHARSET=latin1;", [ incompatible_schema, incompatible_table_tablespace, incompatible_tablespace ])
session.run_sql("CREATE VIEW !.! AS SELECT `data` FROM !.!;", [ incompatible_schema, incompatible_view, incompatible_schema, incompatible_table_wrong_engine ])

#@<> count types tables
types_schema_tables = []
types_schema_views = []

for table in session.run_sql("SELECT TABLE_NAME, TABLE_TYPE FROM information_schema.tables WHERE TABLE_SCHEMA = ?", [types_schema]).fetch_all():
    if table[1] == "BASE TABLE":
        types_schema_tables.append(table[0])
    else:
        types_schema_views.append(table[0])

#@<> Analyze the table {VER(>=8.0.0)}
session.run_sql("ANALYZE TABLE !.! UPDATE HISTOGRAM ON `id`;", [ test_schema, test_table_no_index ])

#@<> first parameter
EXPECT_FAIL("TypeError", "Argument #1 is expected to be an array of strings", None, test_output_relative)
EXPECT_FAIL("TypeError", "Argument #1 is expected to be an array", 1, test_output_relative)
EXPECT_FAIL("TypeError", "Argument #1 is expected to be an array", types_schema, test_output_relative)
EXPECT_FAIL("TypeError", "Argument #1 is expected to be an array", {}, test_output_relative)
EXPECT_FAIL("TypeError", "Argument #1 is expected to be an array of strings", [1], test_output_relative)

#@<> WL13807-TSFR_2_3_3 - second parameter
EXPECT_FAIL("TypeError", "Argument #2 is expected to be a string", [types_schema], None)
EXPECT_FAIL("TypeError", "Argument #2 is expected to be a string", [types_schema], 1)
EXPECT_FAIL("TypeError", "Argument #2 is expected to be a string", [types_schema], [])
EXPECT_FAIL("TypeError", "Argument #2 is expected to be a string", [types_schema], {})
EXPECT_FAIL("TypeError", "Argument #2 is expected to be a string", [types_schema], True)

#@<> WL13807-TSFR_3_1 - third parameter
EXPECT_FAIL("TypeError", "Argument #3 is expected to be a map", [types_schema], test_output_relative, 1)
EXPECT_FAIL("TypeError", "Argument #3 is expected to be a map", [types_schema], test_output_relative, "string")
EXPECT_FAIL("TypeError", "Argument #3 is expected to be a map", [types_schema], test_output_relative, [])

#@<> WL13807-TSFR_2_2 - Call dumpSchemas(): giving less parameters than allowed, giving more parameters than allowed
EXPECT_THROWS(lambda: util.dump_schemas(), "ValueError: Util.dump_schemas: Invalid number of arguments, expected 2 to 3 but got 0")
EXPECT_THROWS(lambda: util.dump_schemas(["mysql"], test_output_relative, {}, None), "ValueError: Util.dump_schemas: Invalid number of arguments, expected 2 to 3 but got 4")

#@<> WL13807-FR2.1 - If the `schemas` parameter is an empty list, an exception must be thrown.
EXPECT_FAIL("ValueError", "The 'schemas' parameter cannot be an empty list.", [], test_output_relative)

#@<> WL13807-FR2.2 - If the `schemas` parameter contains a name of the schema that does not exist, an exception must be thrown.
EXPECT_FAIL("ValueError", "Following schemas were not found in the database: 'mysqlmysql'", ["mysqlmysql"], test_output_relative)
EXPECT_FAIL("ValueError", "Following schemas were not found in the database: 'mysqlmysql'", [types_schema, "mysqlmysql"], test_output_relative)
EXPECT_FAIL("ValueError", "Following schemas were not found in the database: 'mysqlmysql'", ["mysqlmysql", types_schema], test_output_relative)
# WL13807-TSFR_2_1_1
EXPECT_FAIL("ValueError", "Following schemas were not found in the database: 'dummy', 'mysqlmysql'", ["mysqlmysql", types_schema, "dummy"], test_output_relative)
# WL13807-TSFR_2_1_2
EXPECT_FAIL("ValueError", "Following schemas were not found in the database: ''", [""], test_output_relative)

# WL13807-FR2.3 - The `outputUrl` parameter must be handled the same as specified in FR1.1, FR1.1.1 - FR1.1.6.

#@<> WL13807-FR1.1 - The `outputUrl` parameter must be a string value which specifies the output directory, where the dump data is going to be stored.
EXPECT_SUCCESS([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.1 - If the dump is going to be stored on the local filesystem, the `outputUrl` parameter may be optionally prefixed with `file://` scheme.
EXPECT_SUCCESS([types_schema], "file://" + test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.2 - If the dump is going to be stored on the local filesystem and the `outputUrl` parameter holds a relative path, its absolute value is computed as relative to the current working directory.
EXPECT_SUCCESS([types_schema], test_output_relative, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.2 - relative with .
EXPECT_SUCCESS([types_schema], "./" + test_output_relative, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.2 - relative with ..
EXPECT_SUCCESS([types_schema], "dummy/../" + test_output_relative, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.1 + WL13807-FR1.1.2
EXPECT_SUCCESS([types_schema], "file://" + test_output_relative, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR1.1.3 - If the output directory does not exist, it must be created if its parent directory exists. If it is not possible or parent directory does not exist, an exception must be thrown.
# parent directory does not exist
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
EXPECT_FAIL("RuntimeError", "Could not create directory ", [types_schema], os.path.join(test_output_absolute, "dummy"), { "showProgress": False })
EXPECT_FALSE(os.path.isdir(test_output_absolute))

# unable to create directory, file with the same name exists
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
open(test_output_relative, 'a').close()
EXPECT_FAIL("RuntimeError", "Could not create directory ", [types_schema], test_output_absolute, { "showProgress": False })
EXPECT_FALSE(os.path.isdir(test_output_absolute))
os.remove(test_output_relative)

#@<> WL13807-FR1.1.4 - If a local directory is used and the output directory must be created, `rwxr-x---` permissions should be used on the created directory. The owner of the directory is the user running the Shell. {__os_type != "windows"}
EXPECT_SUCCESS([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
# get the umask value
umask = os.umask(0o777)
os.umask(umask)
# use the umask value to compute the expected access rights
EXPECT_EQ(0o750 & ~umask, stat.S_IMODE(os.stat(test_output_absolute).st_mode))
EXPECT_EQ(os.getuid(), os.stat(test_output_absolute).st_uid)

#@<> WL13807-FR1.1.5 - If the output directory exists and is not empty, an exception must be thrown.
# dump into empty directory
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
os.mkdir(test_output_absolute)
util.dump_schemas([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("Schemas dumped: 1")

#@<> dump once again to the same directory, should fail
EXPECT_THROWS(lambda: util.dump_schemas([types_schema], test_output_relative, { "showProgress": False }), "ValueError: Util.dump_schemas: Cannot proceed with the dump, the specified directory '{0}' already exists at the target location {1} and is not empty.".format(test_output_relative, absolute_path_for_output(test_output_absolute)))

# WL13807-FR3 - Both new functions must accept the following options specified in WL#13804, FR5:
# * The `maxRate` option specified in WL#13804, FR5.1.
# * The `showProgress` option specified in WL#13804, FR5.2.
# * The `compression` option specified in WL#13804, FR5.3, with the modification of FR5.3.2, the default value must be`"zstd"`.
# * The `osBucketName` option specified in WL#13804, FR5.4.
# * The `osNamespace` option specified in WL#13804, FR5.5.
# * The `ociConfigFile` option specified in WL#13804, FR5.6.
# * The `ociProfile` option specified in WL#13804, FR5.7.
# * The `defaultCharacterSet` option specified in WL#13804, FR5.8.

#@<> WWL13807-FR4.12 - The `options` dictionary may contain a `chunking` key with a Boolean value, which specifies whether to split the dump data into multiple files.
# WL13807-TSFR_3_52
TEST_BOOL_OPTION("chunking")

EXPECT_SUCCESS([test_schema], test_output_absolute, { "chunking": False, "showProgress": False })
EXPECT_FALSE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + "@"))
EXPECT_FALSE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_unique) + "@"))

#@<> WL13807-FR4.12.3 - If the `chunking` option is not given, a default value of `true` must be used instead.
EXPECT_SUCCESS([test_schema], test_output_absolute, { "showProgress": False })

# WL13807-FR4.12.1 - If the `chunking` option is set to `true` and the index column cannot be selected automatically as described in FR3.1, the data must to written to a single dump file. A warning should be displayed to the user.
# WL13807-FR3.1 - For each table dumped, its index column (name of the column used to order the data and perform the chunking) must be selected automatically as the first column used in the primary key, or if there is no primary key, as the first column used in the first unique index. If the table to be dumped does not contain a primary key and does not contain an unique index, the index column will not be defined.
# WL13807-TSFR_3_521_1
EXPECT_STDOUT_CONTAINS("NOTE: Could not select columns to be used as an index for table `{0}`.`{1}`. Chunking has been disabled for this table, data will be dumped to a single file.".format(test_schema, test_table_non_unique))
EXPECT_STDOUT_CONTAINS("NOTE: Could not select columns to be used as an index for table `{0}`.`{1}`. Chunking has been disabled for this table, data will be dumped to a single file.".format(test_schema, test_table_no_index))

EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_non_unique) + ".tsv.zst")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".tsv.zst")))

# WL13807-FR4.12.2 - If the `chunking` option is set to `true` and the index column can be selected automatically as described in FR3.1, the data must to written to multiple dump files. The data is partitioned into chunks using values from the index column.
# WL13807-TSFR_3_522_1
EXPECT_TRUE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + "@"))
# WL13807-TSFR_3_522_3
EXPECT_TRUE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_unique) + "@"))
number_of_dump_files = count_files_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + "@")

#@<> WL13807-FR4.13 - The `options` dictionary may contain a `bytesPerChunk` key with a string value, which specifies the average estimated number of bytes to be written per each data dump file.
TEST_STRING_OPTION("bytesPerChunk")

EXPECT_SUCCESS([test_schema], test_output_absolute, { "bytesPerChunk": "128k", "showProgress": False })

# WL13807-FR4.13.1 - If the `bytesPerChunk` option is given, it must implicitly set the `chunking` option to `true`.
# WL13807-TSFR_3_531_1
EXPECT_TRUE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + "@"))
EXPECT_TRUE(has_file_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_unique) + "@"))

# WL13807-FR4.13.4 - If the `bytesPerChunk` option is not given and the `chunking` option is set to `true`, a default value of `256M` must be used instead.
# this dump used smaller chunk size, number of files should be greater
EXPECT_TRUE(count_files_with_basename(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + "@") > number_of_dump_files)

#@<> WL13807-FR4.13.2 - The value of the `bytesPerChunk` option must use the same format as specified in WL#12193.
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "xyz"', [types_schema], test_output_absolute, { "bytesPerChunk": "xyz" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "1xyz"', [types_schema], test_output_absolute, { "bytesPerChunk": "1xyz" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "2Mhz"', [types_schema], test_output_absolute, { "bytesPerChunk": "2Mhz" })
# WL13807-TSFR_3_532_3
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "0.1k"', [types_schema], test_output_absolute, { "bytesPerChunk": "0.1k" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "0,3M"', [types_schema], test_output_absolute, { "bytesPerChunk": "0,3M" })
EXPECT_FAIL("ValueError", 'Argument #3: Input number "-1G" cannot be negative', [types_schema], test_output_absolute, { "bytesPerChunk": "-1G" })
EXPECT_FAIL("ValueError", "Argument #3: The option 'bytesPerChunk' cannot be set to an empty string.", [types_schema], test_output_absolute, { "bytesPerChunk": "" })
# WL13807-TSFR_3_532_2
EXPECT_FAIL("ValueError", "Argument #3: The option 'bytesPerChunk' cannot be used if the 'chunking' option is set to false.", [types_schema], test_output_absolute, { "bytesPerChunk": "128k", "chunking": False })

#@<> WL13807-TSFR_3_532_1
EXPECT_SUCCESS([types_schema], test_output_absolute, { "bytesPerChunk": "1000k", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "bytesPerChunk": "1M", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "bytesPerChunk": "1G", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "bytesPerChunk": "128000", "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR4.13.3 - If the value of the `bytesPerChunk` option is smaller than `128k`, an exception must be thrown.
# WL13807-TSFR_3_533_1
EXPECT_FAIL("ValueError", "Argument #3: The value of 'bytesPerChunk' option must be greater or equal to 128k.", [types_schema], test_output_relative, { "bytesPerChunk": "127k" })
EXPECT_FAIL("ValueError", "Argument #3: The value of 'bytesPerChunk' option must be greater or equal to 128k.", [types_schema], test_output_relative, { "bytesPerChunk": "127999" })
EXPECT_FAIL("ValueError", "Argument #3: The value of 'bytesPerChunk' option must be greater or equal to 128k.", [types_schema], test_output_relative, { "bytesPerChunk": "1" })
EXPECT_FAIL("ValueError", "Argument #3: The value of 'bytesPerChunk' option must be greater or equal to 128k.", [types_schema], test_output_relative, { "bytesPerChunk": "0" })
EXPECT_FAIL("ValueError", 'Argument #3: Input number "-1" cannot be negative', [types_schema], test_output_relative, { "bytesPerChunk": "-1" })

#@<> WL13807-FR4.14 - The `options` dictionary may contain a `threads` key with an unsigned integer value, which specifies the number of threads to be used to perform the dump.
# WL13807-TSFR_3_54
TEST_UINT_OPTION("threads")

# WL13807-TSFR_3_54_1
EXPECT_SUCCESS([types_schema], test_output_absolute, { "threads": 2, "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("Running data dump using 2 threads.")

#@<> WL13807-FR4.14.1 - If the value of the `threads` option is set to `0`, an exception must be thrown.
# WL13807-TSFR_3_54
EXPECT_FAIL("ValueError", "Argument #3: The value of 'threads' option must be greater than 0.", [types_schema], test_output_relative, { "threads": 0 })

#@<> WL13807-FR4.14.2 - If the `threads` option is not given, a default value of `4` must be used instead.
# WL13807-TSFR_3_542
EXPECT_SUCCESS([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("Running data dump using 4 threads.")

#@<> WL13807: WL13804-FR5.1 - The `options` dictionary may contain a `maxRate` key with a string value, which specifies the limit of data read throughput in bytes per second per thread.
TEST_STRING_OPTION("maxRate")

# WL13807-TSFR_3_551_1
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1000k", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1000K", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1M", "ddlOnly": True, "showProgress": False })
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1G", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.1 - The value of the `maxRate` option must use the same format as specified in WL#12193.
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "xyz"', [types_schema], test_output_absolute, { "maxRate": "xyz" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "1xyz"', [types_schema], test_output_absolute, { "maxRate": "1xyz" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "2Mhz"', [types_schema], test_output_absolute, { "maxRate": "2Mhz" })
# WL13807-TSFR_3_552
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "hello world!"', [types_schema], test_output_absolute, { "maxRate": "hello world!" })
EXPECT_FAIL("ValueError", 'Argument #3: Input number "-1" cannot be negative', [types_schema], test_output_absolute, { "maxRate": "-1" })
EXPECT_FAIL("ValueError", 'Argument #3: Input number "-1234567890123456" cannot be negative', [types_schema], test_output_absolute, { "maxRate": "-1234567890123456" })
EXPECT_FAIL("ValueError", 'Argument #3: Input number "-2K" cannot be negative', [types_schema], test_output_absolute, { "maxRate": "-2K" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "3m"', [types_schema], test_output_absolute, { "maxRate": "3m" })
EXPECT_FAIL("ValueError", 'Argument #3: Wrong input number "4g"', [types_schema], test_output_absolute, { "maxRate": "4g" })

EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1000000", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.1 - kilo.
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1000k", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.1 - giga.
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "1G", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.2 - If the `maxRate` option is set to `"0"` or to an empty string, the read throughput must not be limited.
# WL13807-TSFR_3_552
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "0", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.2 - empty string.
# WL13807-TSFR_3_552
EXPECT_SUCCESS([types_schema], test_output_absolute, { "maxRate": "", "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.1.2 - missing.
# WL13807-TSFR_3_552
EXPECT_SUCCESS([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807: WL13804-FR5.6 - The `options` dictionary may contain a `showProgress` key with a Boolean value, which specifies whether to display the progress of dump process.
TEST_BOOL_OPTION("showProgress")

EXPECT_SUCCESS([types_schema], test_output_absolute, { "showProgress": True, "ddlOnly": True })

#@<> WL13807: WL13804-FR5.2.1 - The information about the progress must include:
# * The estimated total number of rows to be dumped.
# * The number of rows dumped so far.
# * The current progress as a percentage.
# * The current throughput in rows per second.
# * The current throughput in bytes written per second.
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
rc = testutil.call_mysqlsh([uri, "--", "util", "dump-schemas", types_schema, "--outputUrl", test_output_relative, "--showProgress", "true"])
EXPECT_EQ(0, rc)
EXPECT_TRUE(os.path.isdir(test_output_absolute))
EXPECT_STDOUT_MATCHES(re.compile(r'\d+ thds dumping - \d+% \(\d+\.?\d*[TGMK]? rows / ~\d+\.?\d*[TGMK]? rows\), \d+\.?\d*[TGMK]? rows?/s, \d+\.?\d* [TGMK]?B/s', re.MULTILINE))

#@<> WL13807: WL13804-FR5.2.2 - If the `showProgress` option is not given, a default value of `true` must be used instead if shell is used interactively. Otherwise, it is set to `false`.
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
rc = testutil.call_mysqlsh([uri, "--", "util", "dump-schemas", types_schema, "--outputUrl", test_output_relative])
EXPECT_EQ(0, rc)
EXPECT_TRUE(os.path.isdir(test_output_absolute))
EXPECT_STDOUT_NOT_CONTAINS("rows/s")

#@<> WL13807: WL13804-FR5.3 - The `options` dictionary may contain a `compression` key with a string value, which specifies the compression type used when writing the data dump files.
# WL13807-TSFR_3_57
# WL13807-TSFR_3_571_1
TEST_STRING_OPTION("compression")

# WL13807-TSFR_3_571_2
EXPECT_SUCCESS([types_schema], test_output_absolute, { "compression": "none", "chunking": False, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, types_schema_tables[0]) + ".tsv")))

#@<> WL13807: WL13804-FR5.3.1 - The allowed values for the `compression` option are:
# * `"none"` - no compression is used,
# * `"gzip"` - gzip compression is used.
# * `"zstd"` - zstd compression is used.
# WL13807-TSFR_3_571_3
EXPECT_SUCCESS([types_schema], test_output_absolute, { "compression": "gzip", "chunking": False, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, types_schema_tables[0]) + ".tsv.gz")))

EXPECT_SUCCESS([types_schema], test_output_absolute, { "compression": "zstd", "chunking": False, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, types_schema_tables[0]) + ".tsv.zst")))

EXPECT_FAIL("ValueError", "Argument #3: The option 'compression' cannot be set to an empty string.", [types_schema], test_output_relative, { "compression": "" })
EXPECT_FAIL("ValueError", "Argument #3: Unknown compression type: dummy", [types_schema], test_output_relative, { "compression": "dummy" })

#@<> WL13807: WL13804-FR5.3.2 - If the `compression` option is not given, a default value of `"none"` must be used instead.
# WL13807-FR3 - Both new functions must accept the following options specified in WL#13804, FR5:
# * The `compression` option specified in WL#13804, FR5.3, with the modification of FR5.3.2, the default value must be`"zstd"`.
# WL13807-TSFR_3_572_1
EXPECT_SUCCESS([types_schema], test_output_absolute, { "chunking": False, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, types_schema_tables[0]) + ".tsv.zst")))

#@<> WL13807: WL13804-FR5.4 - The `options` dictionary may contain a `osBucketName` key with a string value, which specifies the OCI bucket name where the data dump files are going to be stored.
# WL13807-TSFR_3_58
TEST_STRING_OPTION("osBucketName")

#@<> WL13807: WL13804-FR5.5 - The `options` dictionary may contain a `osNamespace` key with a string value, which specifies the OCI namespace (tenancy name) where the OCI bucket is located.
# WL13807-TSFR_3_59
TEST_STRING_OPTION("osNamespace")

#@<> WL13807: WL13804-FR5.5.2 - If the value of `osNamespace` option is a non-empty string and the value of `osBucketName` option is an empty string, an exception must be thrown.
# WL13807-TSFR_3_592_1
EXPECT_FAIL("ValueError", "Argument #3: The option 'osNamespace' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "osNamespace": "namespace" })

#@<> WL13807: WL13804-FR5.6 - The `options` dictionary may contain a `ociConfigFile` key with a string value, which specifies the path to the OCI configuration file.
# WL13807-TSFR_3_510_1
TEST_STRING_OPTION("ociConfigFile")

#@<> WL13807: WL13804-FR5.6.2 - If the value of `ociConfigFile` option is a non-empty string and the value of `osBucketName` option is an empty string, an exception must be thrown.
# WL13807-TSFR_3_5102
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociConfigFile' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "ociConfigFile": "config" })

#@<> WL13807: WL13804-FR5.7 - The `options` dictionary may contain a `ociProfile` key with a string value, which specifies the name of the OCI profile to use.
TEST_STRING_OPTION("ociProfile")

#@<> WL13807: WL13804-FR5.7.2 - If the value of `ociProfile` option is a non-empty string and the value of `osBucketName` option is an empty string, an exception must be thrown.
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociProfile' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "ociProfile": "profile" })

#@<> WL14154: WL14154-TSFR1_2 - Validate that the option ociParManifest only take boolean values as valid values.
TEST_BOOL_OPTION("ociParManifest")

#@<> WL14154: WL14154-TSFR1_3 - Validate that the option ociParManifest is valid only when doing a dump to OCI.
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParManifest' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "ociParManifest": True })
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParManifest' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "ociParManifest": False })

#@<> WL14154: WL14154-TSFR2_12 - Doing a dump to file system with ociParManifest set to True and ociParExpireTime set to a valid value. Validate that dump fails because ociParManifest is not valid if osBucketName is not specified.
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParManifest' cannot be used when the value of 'osBucketName' option is not set.", [types_schema], test_output_relative, { "ociParManifest": True, "ociParExpireTime": "2021-01-01" })

#@<> WL14154: WL14154-TSFR2_2 - Validate that the option ociParExpireTime only take string values
TEST_STRING_OPTION("ociParExpireTime")

#@<> WL14154: WL14154-TSFR2_6 - Doing a dump to OCI ociParManifest not set or set to False and ociParExpireTime set to a valid value. Validate that the dump fail because ociParExpireTime it's valid only when ociParManifest is set to True.
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParExpireTime' cannot be used when the value of 'ociParManifest' option is not True.", [types_schema], test_output_relative, { "ociParExpireTime": "2021-01-01" })
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParExpireTime' cannot be used when the value of 'ociParManifest' option is not True.", [types_schema], test_output_relative, { "osBucketName": "bucket", "ociParExpireTime": "2021-01-01" })
EXPECT_FAIL("ValueError", "Argument #3: The option 'ociParExpireTime' cannot be used when the value of 'ociParManifest' option is not True.", [types_schema], test_output_relative, { "osBucketName": "bucket", "ociParManifest": False, "ociParExpireTime": "2021-01-01" })

#@<> WL13807: WL13804-FR5.8 - The `options` dictionary may contain a `defaultCharacterSet` key with a string value, which specifies the character set to be used during the dump. The session variables `character_set_client`, `character_set_connection`, and `character_set_results` must be set to this value for each opened connection.
# WL13807-TSFR4_43
TEST_STRING_OPTION("defaultCharacterSet")

#@<> WL13807-TSFR4_40
EXPECT_SUCCESS([test_schema], test_output_absolute, { "defaultCharacterSet": "utf8mb4", "ddlOnly": True, "showProgress": False })
# name should be correctly encoded using UTF-8
EXPECT_FILE_CONTAINS("CREATE TABLE IF NOT EXISTS `{0}`".format(test_table_non_unique), os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_non_unique) + ".sql"))

#@<> WL13807: WL13804-FR5.8.1 - If the value of the `defaultCharacterSet` option is not a character set supported by the MySQL server, an exception must be thrown.
# WL13807-TSFR4_41
EXPECT_FAIL("MySQL Error (1115)", "Unknown character set: ''", [types_schema], test_output_relative, { "defaultCharacterSet": "" })
EXPECT_FAIL("MySQL Error (1115)", "Unknown character set: 'dummy'", [types_schema], test_output_relative, { "defaultCharacterSet": "dummy" })

#@<> WL13807: WL13804-FR5.8.2 - If the `defaultCharacterSet` option is not given, a default value of `"utf8mb4"` must be used instead.
# WL13807-TSFR4_39
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
# name should be correctly encoded using UTF-8
EXPECT_FILE_CONTAINS("CREATE TABLE IF NOT EXISTS `{0}`".format(test_table_non_unique), os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_non_unique) + ".sql"))

#@<> WL13807-TSFR_3_2 - options param being a dictionary that contains an unknown key
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: dummy", [types_schema], test_output_relative, { "dummy": "fails" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: users", [types_schema], test_output_relative, { "users": "fails" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: excludeUsers", [types_schema], test_output_relative, { "excludeUsers": "fails" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: includeUsers", [types_schema], test_output_relative, { "includeUsers": "fails" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: indexColumn", [types_schema], test_output_relative, { "indexColumn": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: fieldsTerminatedBy", [types_schema], test_output_relative, { "fieldsTerminatedBy": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: fieldsEnclosedBy", [types_schema], test_output_relative, { "fieldsEnclosedBy": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: fieldsEscapedBy", [types_schema], test_output_relative, { "fieldsEscapedBy": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: fieldsOptionallyEnclosed", [types_schema], test_output_relative, { "fieldsOptionallyEnclosed": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: linesTerminatedBy", [types_schema], test_output_relative, { "linesTerminatedBy": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: dialect", [types_schema], test_output_relative, { "dialect": "dummy" })
EXPECT_FAIL("ValueError", "Argument #3: Invalid options: excludeSchemas", [types_schema], test_output_relative, { "excludeSchemas": "dummy" })

# WL13807-FR4 - Both new functions must accept a set of additional options:

#@<> WL13807-FR4.1 - The `options` dictionary may contain a `consistent` key with a Boolean value, which specifies whether the data from the tables should be dumped consistently.
TEST_BOOL_OPTION("consistent")

EXPECT_SUCCESS([types_schema], test_output_absolute, { "consistent": False, "showProgress": False })

#@<> BUG#32107327 verify if consistent dump works

# setup
session.run_sql("CREATE DATABASE db1")
session.run_sql("CREATE TABLE db1.t1 (pk INT PRIMARY KEY AUTO_INCREMENT, c INT NOT NULL)")
session.run_sql("CREATE DATABASE db2")
session.run_sql("CREATE TABLE db2.t1 (pk INT PRIMARY KEY AUTO_INCREMENT, c INT NOT NULL)")

# insert values to both tables in the same transaction
insert_values = """v = 0
session.run_sql("TRUNCATE db1.t1")
session.run_sql("TRUNCATE db2.t1")
while True:
    session.run_sql("BEGIN")
    session.run_sql("INSERT INTO db1.t1 (c) VALUES({0})".format(v))
    session.run_sql("INSERT INTO db2.t1 (c) VALUES({0})".format(v))
    session.run_sql("COMMIT")
    v = v + 1
session.close()
"""

# run a process which constantly inserts some values
pid = testutil.call_mysqlsh_async(["--py", uri], insert_values)

# wait a bit for process to start
time.sleep(1)

# check if dumps for both tables have the same number of entries
for i in range(10):
    EXPECT_SUCCESS(["db1", "db2"], test_output_absolute, { "consistent": True, "showProgress": False, "compression": "none", "chunking": False })
    db1 = os.path.join(test_output_absolute, encode_table_basename("db1", "t1") + ".tsv")
    db2 = os.path.join(test_output_absolute, encode_table_basename("db2", "t1") + ".tsv")
    EXPECT_TRUE(os.path.isfile(db1), "File '{0}' should exist".format(db1))
    EXPECT_TRUE(os.path.isfile(db2), "File '{0}' should exist".format(db2))
    EXPECT_EQ(os.stat(db1).st_size, os.stat(db2).st_size, "Files '{0}' and '{1}' should have the same size".format(db1, db2))

# terminate immediately, process will not stop on its own
testutil.wait_mysqlsh_async(pid, 0)

# cleanup
session.run_sql("DROP DATABASE db1")
session.run_sql("DROP DATABASE db2")

#@<> WL13807-FR4.3 - The `options` dictionary may contain a `events` key with a Boolean value, which specifies whether to include events in the DDL file of each schema.
TEST_BOOL_OPTION("events")

EXPECT_SUCCESS([test_schema], test_output_absolute, { "events": False, "ddlOnly": True, "showProgress": False })
EXPECT_FILE_CONTAINS("CREATE DATABASE /*!32312 IF NOT EXISTS*/ `{0}`".format(test_schema), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))
EXPECT_FILE_NOT_CONTAINS("CREATE DEFINER=`{0}`@`{1}` EVENT IF NOT EXISTS `{2}`".format(__user, __host, test_schema_event), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))

#@<> WL13807-FR4.3.1 - If the `events` option is not given, a default value of `true` must be used instead.
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_FILE_CONTAINS("CREATE DEFINER=`{0}`@`{1}` EVENT IF NOT EXISTS `{2}`".format(__user, __host, test_schema_event), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))

#@<> WL13807-FR4.4 - The `options` dictionary may contain a `routines` key with a Boolean value, which specifies whether to include functions and stored procedures in the DDL file of each schema. User-defined functions must not be included.
TEST_BOOL_OPTION("routines")

EXPECT_SUCCESS([test_schema], test_output_absolute, { "routines": False, "ddlOnly": True, "showProgress": False })
EXPECT_FILE_CONTAINS("CREATE DATABASE /*!32312 IF NOT EXISTS*/ `{0}`".format(test_schema), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))
EXPECT_FILE_NOT_CONTAINS("CREATE DEFINER=`{0}`@`{1}` PROCEDURE `{2}`".format(__user, __host, test_schema_procedure), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))
EXPECT_FILE_NOT_CONTAINS("CREATE DEFINER=`{0}`@`{1}` FUNCTION `{2}`".format(__user, __host, test_schema_function), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))

#@<> WL13807-FR4.4.1 - If the `routines` option is not given, a default value of `true` must be used instead.
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_FILE_CONTAINS("CREATE DEFINER=`{0}`@`{1}` PROCEDURE `{2}`".format(__user, __host, test_schema_procedure), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))
EXPECT_FILE_CONTAINS("CREATE DEFINER=`{0}`@`{1}` FUNCTION `{2}`".format(__user, __host, test_schema_function), os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql"))

#@<> WL13807-FR4.5 - The `options` dictionary may contain a `triggers` key with a Boolean value, which specifies whether to include triggers in the DDL file of each table.
# WL13807-TSFR4_15
TEST_BOOL_OPTION("triggers")

# WL13807-TSFR4_14
# WL13807-TSFR10_5
EXPECT_SUCCESS([test_schema], test_output_absolute, { "triggers": False, "ddlOnly": True, "showProgress": False })
EXPECT_FALSE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".triggers.sql")))

#@<> WL13807-FR4.5.1 - If the `triggers` option is not given, a default value of `true` must be used instead.
# WL13807-TSFR4_13
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".triggers.sql")))

#@<> WL13807-FR4.6 - The `options` dictionary may contain a `tzUtc` key with a Boolean value, which specifies whether to set the time zone to UTC and include a `SET TIME_ZONE='+00:00'` statement in the DDL files and to execute this statement before the data dump is started. This allows dumping TIMESTAMP data when a server has data in different time zones or data is being moved between servers with different time zones.
# WL13807-TSFR4_18
TEST_BOOL_OPTION("tzUtc")

# WL13807-TSFR4_17
EXPECT_SUCCESS([test_schema], test_output_absolute, { "tzUtc": False, "ddlOnly": True, "showProgress": False })
with open(os.path.join(test_output_absolute, "@.json"), encoding="utf-8") as json_file:
    EXPECT_FALSE(json.load(json_file)["tzUtc"])

#@<> WL13807-FR4.6.1 - If the `tzUtc` option is not given, a default value of `true` must be used instead.
# WL13807-TSFR4_16
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
with open(os.path.join(test_output_absolute, "@.json"), encoding="utf-8") as json_file:
    EXPECT_TRUE(json.load(json_file)["tzUtc"])

#@<> WL13807-FR4.7 - The `options` dictionary may contain a `ddlOnly` key with a Boolean value, which specifies whether the data dump should be skipped.
# WL13807-TSFR4_21
TEST_BOOL_OPTION("ddlOnly")

# WL13807-FR4.7.1 - If the `ddlOnly` option is set to `true`, only the DDL files must be written.
# WL13807-TSFR4_20
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_EQ(0, count_files_with_extension(test_output_absolute, ".zst"))
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".sql"))

#@<> WL13807-FR4.7.2 - If the `ddlOnly` option is not given, a default value of `false` must be used instead.
# WL13807-TSFR4_19
EXPECT_SUCCESS([test_schema], test_output_absolute, { "showProgress": False })
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".zst"))
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".sql"))

#@<> WL13807-FR4.8 - The `options` dictionary may contain a `dataOnly` key with a Boolean value, which specifies whether the DDL files should be written.
# WL13807-TSFR4_24
TEST_BOOL_OPTION("dataOnly")

# WL13807-FR4.8.1 - If the `dataOnly` option is set to `true`, only the data dump files must be written.
# WL13807-TSFR4_23
EXPECT_SUCCESS([test_schema], test_output_absolute, { "dataOnly": True, "showProgress": False })
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".zst"))
# WL13807-TSFR9_2
# WL13807-TSFR10_2
# WL13807-TSFR11_2
EXPECT_EQ(0, count_files_with_extension(test_output_absolute, ".sql"))

#@<> WL13807-FR4.8.2 - If both `ddlOnly` and `dataOnly` options are set to `true`, an exception must be raised.
# WL13807-TSFR4_25
EXPECT_FAIL("ValueError", "Argument #3: The 'ddlOnly' and 'dataOnly' options cannot be both set to true.", [types_schema], test_output_relative, { "ddlOnly": True, "dataOnly": True })

#@<> WL13807-FR4.8.3 - If the `dataOnly` option is not given, a default value of `false` must be used instead.
# WL13807-TSFR4_22
EXPECT_SUCCESS([test_schema], test_output_absolute, { "showProgress": False })
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".zst"))
EXPECT_NE(0, count_files_with_extension(test_output_absolute, ".sql"))

#@<> WL13807-FR4.9 - The `options` dictionary may contain a `dryRun` key with a Boolean value, which specifies whether a dry run should be performed.
# WL13807-TSFR4_28
TEST_BOOL_OPTION("dryRun")

# WL13807-FR4.9.1 - If the `dryRun` option is set to `true`, Shell must print information about what would be performed/dumped, including the compatibility notes for `ocimds` if available, but do not dump anything.
# WL13807-TSFR4_27
shutil.rmtree(test_output_absolute, True)
EXPECT_FALSE(os.path.isdir(test_output_absolute))
util.dump_schemas([test_schema], test_output_absolute, { "dryRun": True, "showProgress": False })
EXPECT_FALSE(os.path.isdir(test_output_absolute))
EXPECT_STDOUT_NOT_CONTAINS("Schemas dumped: 1")
EXPECT_STDOUT_CONTAINS("dryRun enabled, no locks will be acquired and no files will be created.")

#@<> WL13807-FR4.9.2 - If the `dryRun` option is not given, a default value of `false` must be used instead.
# WL13807-TSFR4_26
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR4.11 - The `options` dictionary may contain an `excludeTables` key with a list of strings, which specifies the names of tables to be excluded from the dump.
# WL13807-TSFR4_38
TEST_ARRAY_OF_STRINGS_OPTION("excludeTables")

EXPECT_SUCCESS([test_schema], test_output_absolute, { "excludeTables": [], "ddlOnly": True, "showProgress": False })

# WL13807-TSFR4_33
# WL13807-TSFR4_35
# WL13807-TSFR4_37
EXPECT_SUCCESS([test_schema], test_output_absolute, { "excludeTables": [ "`{0}`.`{1}`".format(test_schema, test_table_non_unique), "`{0}`.`{1}`".format(world_x_schema, world_x_table) ], "ddlOnly": True, "showProgress": False })
# WL13807-TSFR10_3
EXPECT_FALSE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_non_unique) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_unique) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".sql")))

#@<> WL13807-FR4.11.1 - The table names must be in form `schema.table`. Both `schema` and `table` must be valid MySQL identifiers and must be quoted with backtick (`` ` ``) character when required.
# WL13807-TSFR4_36
EXPECT_FAIL("ValueError", "Argument #3: The table to be excluded must be in the following form: schema.table, with optional backtick quotes, wrong value: 'dummy'.", [types_schema], test_output_relative, { "excludeTables": [ "dummy" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded 'dummy.dummy.dummy': Invalid object name, expected end of name, but got: '.'", [types_schema], test_output_relative, { "excludeTables": [ "dummy.dummy.dummy" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded '@.dummy': Invalid character in identifier", [types_schema], test_output_relative, { "excludeTables": [ "@.dummy" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded 'dummy.@': Invalid character in identifier", [types_schema], test_output_relative, { "excludeTables": [ "dummy.@" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded '@.@': Invalid character in identifier", [types_schema], test_output_relative, { "excludeTables": [ "@.@" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded '1.dummy': Invalid identifier: identifiers may begin with a digit but unless quoted may not consist solely of digits.", [types_schema], test_output_relative, { "excludeTables": [ "1.dummy" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded 'dummy.1': Invalid identifier: identifiers may begin with a digit but unless quoted may not consist solely of digits.", [types_schema], test_output_relative, { "excludeTables": [ "dummy.1" ] })
EXPECT_FAIL("ValueError", "Argument #3: Failed to parse table to be excluded '2.1': Invalid identifier: identifiers may begin with a digit but unless quoted may not consist solely of digits.", [types_schema], test_output_relative, { "excludeTables": [ "2.1" ] })

#@<> WL13807-FR4.11.2 - If the specified table does not exist in the schema, or the schema is not included in dump, the table name is discarded.
# WL13807-TSFR4_34
EXPECT_SUCCESS([test_schema], test_output_absolute, { "excludeTables": [ "`{0}`.`dummy`".format(test_schema), "`@`.dummy", "dummy.`@`", "`@`.`@`", "`1`.dummy", "dummy.`1`", "`2`.`1`" ], "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR4.11.3 - If the `excludeTables` option is not given, a default value of an empty list must be used instead.
# WL13807-TSFR4_32
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_non_unique) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_primary) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_unique) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".sql")))

#@<> WL13807-FR6 - The data dumps for the following tables must be excluded from any dumps that include their respective schemas. DDL must still be generated:
# * `mysql.apply_status`
# * `mysql.general_log`
# * `mysql.schema`
# * `mysql.slow_log`
# WL13807-TSFR6_1
EXPECT_SUCCESS(["mysql"], test_output_absolute, { "chunking": False, "showProgress": False })

for table in ["apply_status", "general_log", "schema", "slow_log"]:
    EXPECT_FALSE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename("mysql", table) + ".tsv.zst")))
    exists = os.path.isfile(os.path.join(test_output_absolute, encode_table_basename("mysql", table) + ".sql"))
    if 1 == session.run_sql("SELECT COUNT(*) FROM information_schema.tables WHERE TABLE_SCHEMA = 'mysql' AND TABLE_NAME = ?", [table]).fetch_one()[0]:
        EXPECT_TRUE(exists)
    else:
        EXPECT_FALSE(exists)

EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename("mysql", "user") + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename("mysql", "user") + ".tsv.zst")))

#@<> run dump for all SQL-related tests below
EXPECT_SUCCESS([types_schema, test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR9 - For each schema dumped, a DDL file with the base name as specified in FR8 and `.sql` extension must be created in the output directory.
# * The schema DDL file must contain all objects being dumped, including routines and events if enabled by options.
# WL13807-TSFR9_1
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_schema_basename(types_schema) + ".sql")))
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_schema_basename(test_schema) + ".sql")))

#@<> WL13807-FR10 - For each table dumped, a DDL file must be created with a base name as specified in FR7.1, and `.sql` extension.
# * The table DDL file must contain all objects being dumped.
# WL13807-TSFR10_1
for table in types_schema_tables:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, table) + ".sql")))

for table in [test_table_primary, test_table_unique, test_table_non_unique, test_table_no_index]:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, table) + ".sql")))

#@<> WL13807-FR10.1 - If the `triggers` option was set to `true` and the dumped table has triggers, a DDL file must be created with a base name as specified in FR7.1, and `.triggers.sql` extension. File must contain all triggers for that table.
# WL13807-TSFR10_4
EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, test_table_no_index) + ".triggers.sql")))

for table in [test_table_primary, test_table_unique, test_table_non_unique]:
    EXPECT_FALSE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, table) + ".triggers.sql")))

#@<> WL13807-FR11 - For each view dumped, a DDL file must be created with a base name as specified in FR7.1, and `.sql` extension.
# WL13807-TSFR11_1
for view in types_schema_views:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, view) + ".sql")))

for view in [test_view]:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, view) + ".sql")))

#@<> WL13807-FR11.1 - For each view dumped, a DDL file must be created with a base name as specified in FR7.1, and `.pre.sql` extension. This file must contain SQL statements which create a table with the same structure as the dumped view.
# WL13807-TSFR11_3
for view in types_schema_views:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(types_schema, view) + ".pre.sql")))

for view in [test_view]:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, encode_table_basename(test_schema, view) + ".pre.sql")))

#@<> WL13807-FR12 - The following DDL files must be created in the output directory:
# * A file with the name `@.sql`, containing the SQL statements which should be executed before the whole dump is imported.
# * A file with the name `@.post.sql`, containing the SQL statements which should be executed after the whole dump is imported.
# WL13807-TSFR12_1
for f in [ "@.sql", "@.post.sql" ]:
    EXPECT_TRUE(os.path.isfile(os.path.join(test_output_absolute, f)))

#@<> WL13807-FR13 - Once the dump is complete, in addition to the summary described in WL#13804, FR15, the following information must be presented to the user:
# * The number of schemas dumped.
# * The number of tables dumped.
# WL13807: WL13804-FR15 - Once the dump is complete, the summary of the export process must be presented to the user. It must contain:
# * The number of rows written.
# * The number of bytes actually written to the data dump files.
# * The number of data bytes written to the data dump files. (only if `compression` option is not set to `none`)
# * The average throughout in bytes written per second.
# WL13807-TSFR13_1
EXPECT_SUCCESS([types_schema, test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

EXPECT_STDOUT_CONTAINS("Schemas dumped: 2")
EXPECT_STDOUT_CONTAINS("Tables dumped: {0}".format(len(types_schema_tables) + 4))
EXPECT_STDOUT_CONTAINS("Uncompressed data size: 0 bytes")
EXPECT_STDOUT_CONTAINS("Compressed data size: 0 bytes")
EXPECT_STDOUT_CONTAINS("Rows written: 0")
EXPECT_STDOUT_CONTAINS("Bytes written: 0 bytes")
EXPECT_STDOUT_CONTAINS("Average uncompressed throughput: 0.00 B/s")
EXPECT_STDOUT_CONTAINS("Average compressed throughput: 0.00 B/s")

# test without compression
EXPECT_SUCCESS([types_schema, test_schema], test_output_absolute, { "compression": "none", "ddlOnly": True, "showProgress": False })

EXPECT_STDOUT_CONTAINS("Schemas dumped: 2")
EXPECT_STDOUT_CONTAINS("Tables dumped: {0}".format(len(types_schema_tables) + 4))
EXPECT_STDOUT_CONTAINS("Data size:")
EXPECT_STDOUT_CONTAINS("Rows written: 0")
EXPECT_STDOUT_CONTAINS("Bytes written: 0 bytes")
EXPECT_STDOUT_CONTAINS("Average throughput: 0.00 B/s")

#@<> WL13807-FR14 - SQL scripts for DDL must be idempotent. That is, executing the same script multiple times (including when some of the attempts have failed) should result in the same schema that would be left by a single successful execution.
# * All SQL statements which create objects must not fail if object with the same type and name already exist.
# * Existing schemas and tables with the same names as the ones being dumped must not be deleted.
# * Existing views with the same names as the ones being dumped may be deleted.
# * Existing functions, stored procedures and events with the same names as the ones being dumped must be deleted.
# * If the `triggers` option was set to `true`, existing triggers with the same names as the ones being dumped must be deleted.
EXPECT_SUCCESS([test_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

all_sql_files = []
# order is important
all_sql_files.append("@.sql")
all_sql_files.append(encode_schema_basename(test_schema) + ".sql")

for table in [test_table_primary, test_table_unique, test_table_non_unique, test_table_no_index]:
    all_sql_files.append(encode_table_basename(test_schema, table) + ".sql")

for view in [test_view]:
    all_sql_files.append(encode_table_basename(test_schema, view) + ".pre.sql")

for view in [test_view]:
    all_sql_files.append(encode_table_basename(test_schema, view) + ".sql")

all_sql_files.append(encode_table_basename(test_schema, test_table_no_index) + ".triggers.sql")
all_sql_files.append("@.post.sql")

recreate_verification_schema()

for f in all_sql_files:
    for i in range(0, 2):
        EXPECT_EQ(0, testutil.call_mysqlsh([uri + "/" + verification_schema, "--sql", "--file", os.path.join(test_output_absolute, f)]))

drop_all_schemas(all_schemas)

#@<> WL13807-FR15 - All created files should have permissions set to rw-r-----, owner should be set to the user running Shell. {__os_type != "windows"}
# WL13807-TSFR15_1
EXPECT_SUCCESS([test_schema], test_output_absolute, { "showProgress": False })

for f in os.listdir(test_output_absolute):
    path = os.path.join(test_output_absolute, f)
    EXPECT_EQ(0o640, stat.S_IMODE(os.stat(path).st_mode))
    EXPECT_EQ(os.getuid(), os.stat(path).st_uid)

#@<> WL13807-FR16.1 - The `options` dictionary may contain a `ocimds` key with a Boolean or string value, which specifies whether the compatibility checks with `MySQL Database Service` and DDL substitutions should be done.
TEST_BOOL_OPTION("ocimds")

#@<> tables with missing PKs
missing_pks = {
    "xtest": [
        "t_bigint",
        "t_bit",
        "t_char",
        "t_date",
        "t_decimal1",
        "t_decimal2",
        "t_decimal3",
        "t_double",
        "t_enum",
        "t_float",
        "t_geom",
        "t_geom_all",
        "t_int",
        "t_integer",
        "t_json",
        "t_lchar",
        "t_lob",
        "t_mediumint",
        "t_numeric1",
        "t_numeric2",
        "t_real",
        "t_set",
        "t_smallint",
        "t_tinyint",
    ],
    incompatible_schema: [
        incompatible_table_data_directory,
        incompatible_table_encryption,
        incompatible_table_index_directory,
        incompatible_table_tablespace,
        incompatible_table_wrong_engine,
    ],
    test_schema: [
        test_table_unique,
        test_table_non_unique,
        test_table_no_index
    ]
}

#@<> WL13807-FR16.1.1 - If the `ocimds` option is set to `true`, the following must be done:
# * General
#   * Add the `mysql` schema to the schema exclusion list
# * GRANT
#   * Check whether users or roles are granted the following privileges and error out if so.
#     * SUPER,
#     * FILE,
#     * RELOAD,
#     * BINLOG_ADMIN,
#     * SET_USER_ID.
# * CREATE TABLE
#   * If ENGINE is not `InnoDB`, an exception must be raised.
#   * DATA|INDEX DIRECTORY and ENCRYPTION options must be commented out.
#   * Same restrictions for partitions.
# * CHARSETS - checks whether db objects use any other character set than supported utf8mb4.
# WL13807-TSFR16_1
# WL14506-TSFR_1_6
excluded_tables = []

for table in missing_pks[test_schema]:
    excluded_tables.append("`{0}`.`{1}`".format(test_schema, table))

EXPECT_FAIL("RuntimeError", "Compatibility issues were found", [incompatible_schema, test_schema], test_output_relative, { "ocimds": True, "excludeTables": excluded_tables })
EXPECT_STDOUT_CONTAINS("Checking for compatibility with MySQL Database Service {0}".format(__mysh_version_no_extra))

if __version_num < 80000:
    EXPECT_STDOUT_CONTAINS("NOTE: MySQL Server 5.7 detected, please consider upgrading to 8.0 first.")
    EXPECT_STDOUT_CONTAINS("NOTE: You can check for potential upgrade issues using util.check_for_server_upgrade().")

EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had {{DATA|INDEX}} DIRECTORY table option commented out".format(incompatible_schema, incompatible_table_data_directory))

EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had ENCRYPTION table option commented out".format(incompatible_schema, incompatible_table_encryption))

if __version_num < 80000 and __os_type != "windows":
    EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had {{DATA|INDEX}} DIRECTORY table option commented out".format(incompatible_schema, incompatible_table_index_directory))

EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' uses unsupported storage engine MyISAM (fix this with 'force_innodb' compatibility option)".format(incompatible_schema, incompatible_table_index_directory))

EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' uses unsupported tablespace option (fix this with 'strip_tablespaces' compatibility option)".format(incompatible_schema, incompatible_table_tablespace))

EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' uses unsupported storage engine MyISAM (fix this with 'force_innodb' compatibility option)".format(incompatible_schema, incompatible_table_wrong_engine))

EXPECT_STDOUT_CONTAINS("ERROR: View {0}.{1} - definition uses DEFINER clause set to user `root`@`localhost` which can only be executed by this user or a user with SET_USER_ID or SUPER privileges (fix this with 'strip_definers' compatibility option)".format(incompatible_schema, incompatible_view))

EXPECT_STDOUT_CONTAINS("Compatibility issues with MySQL Database Service {0} were found. Please use the 'compatibility' option to apply compatibility adaptations to the dumped DDL.".format(__mysh_version_no_extra))

# WL14506-FR1 - When a dump is executed with the ocimds option set to true, for each table that would be dumped which does not contain a primary key, an error must be reported.
# WL14506-TSFR_1_11
for table in missing_pks[incompatible_schema]:
    EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, which is required for High Availability in MDS".format(incompatible_schema, table))

# WL14506-TSFR_1_6
for table in missing_pks["xtest"] + missing_pks[test_schema]:
    EXPECT_STDOUT_NOT_CONTAINS(table)

# WL14506-FR1.1 - The following message must be printed:
# WL14506-TSFR_1_11
EXPECT_STDOUT_CONTAINS("""
ERROR: One or more tables without Primary Keys were found.

       MySQL Database Service High Availability (MDS HA) requires Primary Keys to be present in all tables.
       To continue with the dump you must do one of the following:

       * Create PRIMARY keys in all tables before dumping them.
         MySQL 8.0.23 supports the creation of invisible columns to allow creating Primary Key columns with no impact to applications. For more details, see https://dev.mysql.com/doc/refman/en/invisible-columns.html.
         This is considered a best practice for both performance and usability and will work seamlessly with MDS.

       * Add the "create_invisible_pks" to the "compatibility" option.
         The dump will proceed and loader will automatically add Primary Keys to tables that don't have them when loading into MDS.
         This will make it possible to enable HA in MDS without application impact.
         However, Inbound Replication into an MDS HA instance (at the time of the release of MySQL Shell 8.0.24) will still not be possible.

       * Add the "ignore_missing_pks" to the "compatibility" option.
         This will disable this check and the dump will be produced normally, Primary Keys will not be added automatically.
         It will not be possible to load the dump in an HA enabled MDS instance.
""")

#@<> WL14506-TSFR_2_1
util.help("dump_schemas")

EXPECT_STDOUT_CONTAINS("""
      create_invisible_pks - Each table which does not have a Primary Key will
      have one created when the dump is loaded. The following Primary Key is
      added to the table:
      `my_row_id` BIGINT UNSIGNED AUTO_INCREMENT INVISIBLE PRIMARY KEY

      At the time of the release of MySQL Shell 8.0.24, dumps created with this
      value cannot be used with Inbound Replication into an MySQL Database
      Service instance with High Availability. Mutually exclusive with the
      ignore_missing_pks value.
""")
EXPECT_STDOUT_CONTAINS("""
      ignore_missing_pks - Ignore errors caused by tables which do not have
      Primary Keys. Dumps created with this value cannot be used in MySQL
      Database Service instance with High Availability. Mutually exclusive with
      the create_invisible_pks value.
""")
EXPECT_STDOUT_CONTAINS("""
      At the time of the release of MySQL Shell 8.0.24, in order to use Inbound
      Replication into an MySQL Database Service instance with High
      Availability, all tables at the source server need to have Primary Keys.
      This needs to be fixed manually before running the dump. Starting with
      MySQL 8.0.23 invisible columns may be used to add Primary Keys without
      changing the schema compatibility, for more information see:
      https://dev.mysql.com/doc/refman/en/invisible-columns.html.

      In order to use MySQL Database Service instance with High Availability,
      all tables at the MDS server need to have Primary Keys. This can be fixed
      automatically using the create_invisible_pks compatibility value.
""")

#@<> WL14506-TSFR_1_7
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "ocimds": True, "dataOnly": True, "showProgress": False })

#@<> WL13807-FR16.1.2 - If the `ocimds` option is not given, a default value of `false` must be used instead.
# WL13807-TSFR16_2
# WL14506-TSFR_1_5
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

#@<> WL13807-FR16.2 - The `options` dictionary may contain a `compatibility` key with an array of strings value, which specifies the `MySQL Database Service`-related compatibility modifications that should be applied when creating the DDL files.
TEST_ARRAY_OF_STRINGS_OPTION("compatibility")

EXPECT_FAIL("ValueError", "Argument #3: Unknown compatibility option: dummy", [incompatible_schema], test_output_relative, { "compatibility": [ "dummy" ] })
EXPECT_FAIL("ValueError", "Argument #3: Unknown compatibility option: ", [incompatible_schema], test_output_relative, { "compatibility": [ "" ] })

#@<> WL13807-FR16.2.1 - The `compatibility` option may contain the following values:
# * `force_innodb` - replace incompatible table engines with `InnoDB`,
# * `strip_restricted_grants` - remove disallowed grants.
# * `strip_tablespaces` - remove unsupported tablespace syntax.
# * `strip_definers` - remove DEFINER clause from views, triggers, events and routines and change SQL SECURITY property to INVOKER for views and routines.
# WL14506-FR2 - The compatibility option of the dump utilities must support a new value: ignore_missing_pks.
# WL14506-TSFR_1_12
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "ocimds": True, "compatibility": [ "force_innodb", "ignore_missing_pks", "strip_definers", "strip_restricted_grants", "strip_tablespaces" ] , "ddlOnly": True, "showProgress": False })

# WL14506-FR2.2 - The following message must be printed:
# WL14506-TSFR_1_12
EXPECT_STDOUT_CONTAINS("""
NOTE: One or more tables without Primary Keys were found.

      This issue is ignored.
      This dump cannot be loaded into an MySQL Database Service instance with High Availability.
""")

# WL14506-FR3 - The compatibility option of the dump utilities must support a new value: create_invisible_pks.
# create_invisible_pks and ignore_missing_pks are mutually exclusive
# WL14506-TSFR_1_13
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "ocimds": True, "compatibility": [ "create_invisible_pks", "force_innodb", "strip_definers", "strip_restricted_grants", "strip_tablespaces" ] , "ddlOnly": True, "showProgress": False })

# WL14506-FR3.1.1 - The following message must be printed:
# WL14506-TSFR_1_13
EXPECT_STDOUT_CONTAINS("""
NOTE: One or more tables without Primary Keys were found.

      Missing Primary Keys will be created automatically when this dump is loaded.
      This will make it possible to enable High Availability in MySQL Database Service instance without application impact.
      However, Inbound Replication into an MDS HA instance (at the time of the release of MySQL Shell 8.0.24) will still not be possible.
""")

#@<> WL14506-FR3.1 - When a dump is executed with the ocimds option set to true and the compatibility option contains the create_invisible_pks value, for each table that would be dumped which does not contain a primary key, an information should be printed that an invisible primary key will be created when loading the dump.
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "compatibility": [ "create_invisible_pks" ] , "ddlOnly": True, "showProgress": False })

for table in missing_pks[incompatible_schema]:
    EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' does not have a Primary Key, this will be fixed when the dump is loaded".format(incompatible_schema, table))

#@<> WL14506-FR3.2 - When a dump is executed and the compatibility option contains the create_invisible_pks value, for each table that would be dumped which does not contain a primary key but has a column named my_row_id, an error must be reported.
# WL14506-TSFR_3.2_2
table = missing_pks[incompatible_schema][0]
session.run_sql("ALTER TABLE !.! ADD COLUMN my_row_id int;", [incompatible_schema, table])

EXPECT_FAIL("RuntimeError", "Fatal error during dump", [incompatible_schema], test_output_relative, { "compatibility": [ "create_invisible_pks" ] }, True)
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("Could not apply some of the compatibility options")

WIPE_OUTPUT()
EXPECT_FAIL("RuntimeError", "Compatibility issues were found", [incompatible_schema], test_output_relative, { "ocimds": True, "compatibility": [ "create_invisible_pks" ] })
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))

session.run_sql("ALTER TABLE !.! DROP COLUMN my_row_id;", [incompatible_schema, table])

#@<> WL14506-FR3.4 - When a dump is executed and the compatibility option contains the create_invisible_pks value, for each table that would be dumped which does not contain a primary key but has a column with an AUTO_INCREMENT attribute, an error must be reported.
table = missing_pks[incompatible_schema][0]
session.run_sql("ALTER TABLE !.! ADD COLUMN idx int AUTO_INCREMENT UNIQUE;", [incompatible_schema, table])

EXPECT_FAIL("RuntimeError", "Fatal error during dump", [incompatible_schema], test_output_relative, { "compatibility": [ "create_invisible_pks" ] }, True)
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("Could not apply some of the compatibility options")

WIPE_OUTPUT()
EXPECT_FAIL("RuntimeError", "Compatibility issues were found", [incompatible_schema], test_output_relative, { "ocimds": True, "compatibility": [ "create_invisible_pks" ] })
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))

session.run_sql("ALTER TABLE !.! DROP COLUMN idx;", [incompatible_schema, table])

#@<> WL14506-FR3.2 + WL14506-FR3.4 - same column
table = missing_pks[incompatible_schema][0]
session.run_sql("ALTER TABLE !.! ADD COLUMN my_row_id int AUTO_INCREMENT UNIQUE;", [incompatible_schema, table])

EXPECT_FAIL("RuntimeError", "Fatal error during dump", [incompatible_schema], test_output_relative, { "compatibility": [ "create_invisible_pks" ] }, True)
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("Could not apply some of the compatibility options")

WIPE_OUTPUT()
EXPECT_FAIL("RuntimeError", "Compatibility issues were found", [incompatible_schema], test_output_relative, { "ocimds": True, "compatibility": [ "create_invisible_pks" ] })
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))

session.run_sql("ALTER TABLE !.! DROP COLUMN my_row_id;", [incompatible_schema, table])

#@<> WL14506-FR3.2 + WL14506-FR3.4 - different columns
table = missing_pks[incompatible_schema][0]
session.run_sql("ALTER TABLE !.! ADD COLUMN my_row_id int;", [incompatible_schema, table])
session.run_sql("ALTER TABLE !.! ADD COLUMN idx int AUTO_INCREMENT UNIQUE;", [incompatible_schema, table])

EXPECT_FAIL("RuntimeError", "Fatal error during dump", [incompatible_schema], test_output_relative, { "compatibility": [ "create_invisible_pks" ] }, True)
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("Could not apply some of the compatibility options")

WIPE_OUTPUT()
EXPECT_FAIL("RuntimeError", "Compatibility issues were found", [incompatible_schema], test_output_relative, { "ocimds": True, "compatibility": [ "create_invisible_pks" ] })
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column named `my_row_id` (this issue needs to be fixed manually)".format(incompatible_schema, table))
EXPECT_STDOUT_CONTAINS("ERROR: Table '{0}'.'{1}' does not have a Primary Key, this cannot be fixed automatically because the table has a column with 'AUTO_INCREMENT' attribute (this issue needs to be fixed manually)".format(incompatible_schema, table))

session.run_sql("ALTER TABLE !.! DROP COLUMN my_row_id;", [incompatible_schema, table])
session.run_sql("ALTER TABLE !.! DROP COLUMN idx;", [incompatible_schema, table])

#@<> WL14506-FR3.3 - When a dump is executed and the compatibility option contains both create_invisible_pks and ignore_missing_pks values, an error must be reported and process must be aborted.
# WL14506-TSFR_3.3_1
EXPECT_FAIL("ValueError", "Argument #3: The 'create_invisible_pks' and 'ignore_missing_pks' compatibility options cannot be used at the same time.", [incompatible_schema], test_output_relative, { "compatibility": [ "create_invisible_pks", "ignore_missing_pks" ] })

#@<> WL13807-FR16.2.1 - force_innodb
# WL13807-TSFR16_3
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "compatibility": [ "force_innodb" ] , "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported engine MyISAM changed to InnoDB".format(incompatible_schema, incompatible_table_index_directory))
EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported engine MyISAM changed to InnoDB".format(incompatible_schema, incompatible_table_wrong_engine))

#@<> WL14506-FR2.1 - When a dump is executed with the ocimds option set to true and the compatibility option contains the ignore_missing_pks value, for each table that would be dumped which does not contain a primary key, a note must be displayed, stating that this issue is ignored.
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "compatibility": [ "ignore_missing_pks" ] , "ddlOnly": True, "showProgress": False })

for table in missing_pks[incompatible_schema]:
    EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' does not have a Primary Key, this is ignored".format(incompatible_schema, table))

#@<> WL13807-FR16.2.1 - strip_definers
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "compatibility": [ "strip_definers" ] , "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("NOTE: View {0}.{1} had definer clause removed and SQL SECURITY characteristic set to INVOKER".format(incompatible_schema, incompatible_view))

#@<> WL13807-FR16.2.1 - strip_tablespaces
# WL13807-TSFR16_4
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "compatibility": [ "strip_tablespaces" ] , "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported tablespace option removed".format(incompatible_schema, incompatible_table_tablespace))

#@<> WL13807-FR16.2.2 - If the `compatibility` option is not given, a default value of `[]` must be used instead.
# WL13807-TSFR16_6
EXPECT_SUCCESS([incompatible_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })
EXPECT_STDOUT_NOT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported engine MyISAM changed to InnoDB".format(incompatible_schema, incompatible_table_index_directory))
EXPECT_STDOUT_NOT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported engine MyISAM changed to InnoDB".format(incompatible_schema, incompatible_table_wrong_engine))
EXPECT_STDOUT_NOT_CONTAINS("NOTE: View {0}.{1} had definer clause removed and SQL SECURITY characteristic set to INVOKER".format(incompatible_schema, incompatible_view))
EXPECT_STDOUT_NOT_CONTAINS("NOTE: Table '{0}'.'{1}' had unsupported tablespace option removed".format(incompatible_schema, incompatible_table_tablespace))

# WL13807-FR7 - The table data dump must be written in the output directory, to a file with a base name as specified in FR7.1, and a `.tsv` extension.
# For WL13807-FR7.1 please see above.
# WL13807-FR7.2 - If multiple table data dump files are to be created (the `chunking` option is set to `true`), the base name of each file must consist of a base name, as specified in FR6, followed by `@N` (or `@@N` in case of the last file), where `N` is the zero-based ordinal number of the file.
# WL13807-FR7.3 - The format of an output file must be the same as the default format used by the `util.importTable()` function, as specified in WL#12193.
# WL13807-FR7.4 - While the data is being written to an output file, the data dump file must be suffixed with a `.dumping` extension. Once writing is finished, the file must be renamed to its original name.
# Data consistency tests.

#@<> test a single table with no chunking
EXPECT_SUCCESS([world_x_schema], test_output_absolute, { "chunking": False, "compression": "none", "showProgress": False })
TEST_LOAD(world_x_schema, world_x_table, False)

#@<> test multiple tables with chunking
EXPECT_SUCCESS([test_schema], test_output_absolute, { "bytesPerChunk": "1M", "compression": "none", "showProgress": False })
TEST_LOAD(test_schema, test_table_primary, True)
TEST_LOAD(test_schema, test_table_unique, True)
# these are not chunked because they don't have an appropriate column
TEST_LOAD(test_schema, test_table_non_unique, False)
TEST_LOAD(test_schema, test_table_no_index, False)

#@<> test multiple tables with various data types
EXPECT_SUCCESS([types_schema], test_output_absolute, { "chunking": False, "compression": "none", "showProgress": False })

for table in types_schema_tables:
    TEST_LOAD(types_schema, table, False)

#@<> dump table when different character set/SQL mode is used
session.run_sql("SET NAMES 'latin1';")
session.run_sql("SET GLOBAL SQL_MODE='ANSI_QUOTES,NO_AUTO_VALUE_ON_ZERO,NO_BACKSLASH_ESCAPES,NO_DIR_IN_CREATE,NO_ZERO_DATE,PAD_CHAR_TO_FULL_LENGTH';")

EXPECT_SUCCESS([world_x_schema], test_output_absolute, { "defaultCharacterSet": "latin1", "bytesPerChunk": "1M", "compression": "none", "showProgress": False })
TEST_LOAD(world_x_schema, world_x_table, True)

session.run_sql("SET NAMES 'utf8mb4';")
session.run_sql("SET GLOBAL SQL_MODE='';")

#@<> prepare user privileges, switch user
session.run_sql("GRANT ALL ON *.* TO !@!;", [test_user, __host])
session.run_sql("REVOKE RELOAD /*!80023 , FLUSH_TABLES */ ON *.* FROM !@!;", [test_user, __host])
shell.connect("mysql://{0}:{1}@{2}:{3}".format(test_user, test_user_pwd, __host, __mysql_sandbox_port1))

#@<> try to run consistent dump using a user which does not have required privileges for FTWRL but LOCK TABLES are ok
EXPECT_SUCCESS([types_schema], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_CONTAINS("WARNING: The current user lacks privileges to acquire a global read lock using 'FLUSH TABLES WITH READ LOCK'. Falling back to LOCK TABLES...")

#@<> revoke lock tables from mysql.* {VER(>=8.0.16)}
setup_session()
session.run_sql("SET GLOBAL partial_revokes=1")
session.run_sql("REVOKE LOCK TABLES ON mysql.* FROM !@!;", [test_user, __host])
shell.connect("mysql://{0}:{1}@{2}:{3}".format(test_user, test_user_pwd, __host, __mysql_sandbox_port1))

#@<> try again, this time it should succeed but without locking mysql.* tables {VER(>=8.0.16)}
EXPECT_SUCCESS([types_schema], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_CONTAINS("WARNING: The current user lacks privileges to acquire a global read lock using 'FLUSH TABLES WITH READ LOCK'. Falling back to LOCK TABLES...")
EXPECT_STDOUT_CONTAINS("WARNING: Could not lock mysql system tables: User 'sample_user'@'localhost' is missing the following privilege(s) for schema `mysql`: LOCK TABLES.")

#@<> revoke lock tables from the rest
setup_session()
session.run_sql("REVOKE LOCK TABLES ON *.* FROM !@!;", [test_user, __host])
shell.connect("mysql://{0}:{1}@{2}:{3}".format(test_user, test_user_pwd, __host, __mysql_sandbox_port1))

#@<> try to run consistent dump using a user which does not have any required privileges
EXPECT_FAIL("RuntimeError", re.compile(r"Unable to lock tables: User 'sample_user'@'localhost' is missing the following privilege\(s\) for table `.+`\.`.+`: LOCK TABLES."), [types_schema], test_output_absolute, { "showProgress": False })
EXPECT_STDOUT_CONTAINS("WARNING: The current user lacks privileges to acquire a global read lock using 'FLUSH TABLES WITH READ LOCK'. Falling back to LOCK TABLES...")
EXPECT_STDOUT_CONTAINS("ERROR: Unable to acquire global read lock neither table read locks")

#@<> using the same user, run inconsistent dump
EXPECT_SUCCESS([types_schema], test_output_absolute, { "consistent": False, "ddlOnly": True, "showProgress": False })

#@<> reconnect to the user with full privilages, restore test user account
setup_session()
create_user()

#@<> Bug #31188854: USING THE OCIPROFILE OPTION IN A DUMP MAKE THE DUMP TO ALWAYS FAIL {has_oci_environment('OS')}
# This error now confirms the reported issue is fixed
EXPECT_FAIL("RuntimeError", "Failed to get object list", [test_schema], '', {"osBucketName": "any-bucket", "ociProfile": "DEFAULT"})

#@<> An error should occur when dumping using oci+os://
EXPECT_FAIL("ValueError", "Directory handling for oci+os protocol is not supported.", [test_schema], 'oci+os://sakila')

#@<> BUG#32430402 metadata should contain information about binlog
EXPECT_SUCCESS([types_schema], test_output_absolute, { "ddlOnly": True, "showProgress": False })

with open(os.path.join(test_output_absolute, "@.json"), encoding="utf-8") as json_file:
    metadata = json.load(json_file)
    EXPECT_EQ(True, "binlogFile" in metadata, "'binlogFile' should be in metadata")
    EXPECT_EQ(True, "binlogPosition" in metadata, "'binlogPosition' should be in metadata")

#@<> Cleanup
drop_all_schemas()
session.run_sql("SET GLOBAL local_infile = false;")
session.close()
testutil.destroy_sandbox(__mysql_sandbox_port1);
shutil.rmtree(incompatible_table_directory, True)
