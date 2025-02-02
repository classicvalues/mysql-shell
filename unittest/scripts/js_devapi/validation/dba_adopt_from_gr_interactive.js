//@ Initialization
||

//@ it's not possible to adopt from GR without existing group replication
||The adoptFromGR option is set to true, but there is no replication group to adopt (ArgumentError)

//@ Create cluster
||

//@ Adding instance to cluster
||

//@<OUT> Drop Metadata
Are you sure you want to remove the Metadata? [y/N]: Metadata Schema successfully removed.

//@ Check cluster status after drop metadata schema
||This function is not available through a session to an instance belonging to an unmanaged replication group (RuntimeError)

//@<OUT> Create cluster adopting from GR - answer 'yes' to prompt
A new InnoDB cluster will be created on instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'.

You are connected to an instance that belongs to an unmanaged replication group.
Do you want to setup an InnoDB cluster based on this replication group? [Y/n]: Creating InnoDB cluster 'testCluster' on '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...

Adding Seed Instance...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>'...
Resetting distributed recovery credentials across the cluster...
NOTE: User 'mysql_innodb_cluster_1111'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
NOTE: User 'mysql_innodb_cluster_2222'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port1+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port2+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
Cluster successfully created based on existing replication group.

//@<OUT> Confirm new replication users were created and replaced existing ones.
user	host
mysql_innodb_cluster_1111	%
mysql_innodb_cluster_2222	%
2
instance_name	attributes
<<<hostname>>>:<<<__mysql_sandbox_port1>>>	{"server_id": 1111, "recoveryAccountHost": "%", "recoveryAccountUser": "mysql_innodb_cluster_1111"}
<<<hostname>>>:<<<__mysql_sandbox_port2>>>	{"server_id": 2222, "recoveryAccountHost": "%", "recoveryAccountUser": "mysql_innodb_cluster_2222"}
2
recovery_user
mysql_innodb_cluster_1111
1
user	host
mysql_innodb_cluster_1111	%
mysql_innodb_cluster_2222	%
2
instance_name	attributes
<<<hostname>>>:<<<__mysql_sandbox_port1>>>	{"server_id": 1111, "recoveryAccountHost": "%", "recoveryAccountUser": "mysql_innodb_cluster_1111"}
<<<hostname>>>:<<<__mysql_sandbox_port2>>>	{"server_id": 2222, "recoveryAccountHost": "%", "recoveryAccountUser": "mysql_innodb_cluster_2222"}
2
recovery_user
mysql_innodb_cluster_2222
1

//@ Create cluster adopting from GR - answer 'no' to prompt
||Creating a cluster on an unmanaged replication group requires adoptFromGR option to be true (MYSQLSH 51315)

//@ Check cluster status - failure
||The cluster object is disconnected. Please use dba.getCluster to obtain a fresh cluster handle. (RuntimeError)

//@<OUT> Create cluster adopting from GR - use 'adoptFromGR' option
A new InnoDB cluster will be created based on the existing replication group on instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'.

Creating InnoDB cluster 'testCluster' on '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...

Adding Seed Instance...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>'...
Resetting distributed recovery credentials across the cluster...
NOTE: User 'mysql_innodb_cluster_1111'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
NOTE: User 'mysql_innodb_cluster_2222'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port1+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port2+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
Cluster successfully created based on existing replication group.

//@<OUT> Create cluster adopting from multi-primary GR - use 'adoptFromGR' option
A new InnoDB cluster will be created based on the existing replication group on instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'.

Creating InnoDB cluster 'testCluster' on '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...

Adding Seed Instance...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'...
Adding Instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>'...
Resetting distributed recovery credentials across the cluster...
NOTE: User 'mysql_innodb_cluster_1111'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
NOTE: User 'mysql_innodb_cluster_2222'@'%' already existed at instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>'. It will be deleted and created again with a new password.
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port1+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
<<<(__version_num<80011)?"WARNING: Instance '"+hostname+":"+__mysql_sandbox_port2+"' cannot persist configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the dba.configureLocalInstance() command locally to persist the changes.\n":""\>>>
Cluster successfully created based on existing replication group.

//@ dissolve the cluster
|The cluster was successfully dissolved.|

//@ it's not possible to adopt from GR when cluster was dissolved
||The adoptFromGR option is set to true, but there is no replication group to adopt (ArgumentError)

//@ Finalization
||
