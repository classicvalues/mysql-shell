//@ Initialization
||

//@<OUT> Configures the instance, answers 'yes' on the read only prompt
Configuring local MySQL instance listening at port <<<__mysql_sandbox_port1>>> for use in an InnoDB cluster...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>
Assuming full account name 'testUser'@'%' for testUser

The instance 'localhost:<<<__mysql_sandbox_port1>>>' is valid for InnoDB cluster usage.

The MySQL instance at 'localhost:<<<__mysql_sandbox_port1>>>' currently has the super_read_only system
variable set to protect it from inadvertent updates from applications. You must
first unset it to be able to perform any changes to this instance.
For more information see:
https://dev.mysql.com/doc/refman/en/server-system-variables.html#sysvar_super_read_only.

Do you want to disable super_read_only and continue? [y/N]: Disabled super_read_only on the instance 'localhost:<<<__mysql_sandbox_port1>>>'
Cluster admin user 'testUser'@'%' created.
Enabling super_read_only on the instance 'localhost:<<<__mysql_sandbox_port1>>>'

//@<OUT> Configures the instance, read only set, no prompt
Configuring local MySQL instance listening at port <<<__mysql_sandbox_port2>>> for use in an InnoDB cluster...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>
Assuming full account name 'testUser'@'%' for testUser

The instance 'localhost:<<<__mysql_sandbox_port2>>>' is valid for InnoDB cluster usage.
Disabled super_read_only on the instance 'localhost:<<<__mysql_sandbox_port2>>>'
Cluster admin user 'testUser'@'%' created.
Enabling super_read_only on the instance 'localhost:<<<__mysql_sandbox_port2>>>'

//@<OUT> Configures the instance, no prompt
Configuring local MySQL instance listening at port <<<__mysql_sandbox_port3>>> for use in an InnoDB cluster...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>
Assuming full account name 'testUser'@'%' for testUser

The instance 'localhost:<<<__mysql_sandbox_port3>>>' is valid for InnoDB cluster usage.

Cluster admin user 'testUser'@'%' created.

//@<OUT> Creates Cluster succeeds, answers 'yes' on read only prompt {VER(>=8.0.11)}
A new InnoDB cluster will be created on instance 'localhost:<<<__mysql_sandbox_port1>>>'.

Validating instance at localhost:<<<__mysql_sandbox_port1>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.

The MySQL instance at 'localhost:<<<__mysql_sandbox_port1>>>' currently has the super_read_only system
variable set to protect it from inadvertent updates from applications. You must
first unset it to be able to perform any changes to this instance.
For more information see:
https://dev.mysql.com/doc/refman/en/server-system-variables.html#sysvar_super_read_only.

NOTE: There are open sessions to 'localhost:<<<__mysql_sandbox_port1>>>'.
You may want to kill these sessions to prevent them from performing unexpected updates: 

1 open session(s) of 'root@localhost'. 

Do you want to disable super_read_only and continue? [y/N]:
Creating InnoDB cluster 'sample' on 'localhost:<<<__mysql_sandbox_port1>>>'...

Adding Seed Instance...
Cluster successfully created. Use Cluster.addInstance() to add MySQL instances.
At least 3 instances are needed for the cluster to be able to withstand up to
one server failure.

//@<OUT> Creates Cluster succeeds, answers 'yes' on read only prompt {VER(<8.0.11)}
A new InnoDB cluster will be created on instance 'localhost:<<<__mysql_sandbox_port1>>>'.

Validating instance at localhost:<<<__mysql_sandbox_port1>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.

The MySQL instance at 'localhost:<<<__mysql_sandbox_port1>>>' currently has the super_read_only system
variable set to protect it from inadvertent updates from applications. You must
first unset it to be able to perform any changes to this instance.
For more information see:
https://dev.mysql.com/doc/refman/en/server-system-variables.html#sysvar_super_read_only.

NOTE: There are open sessions to 'localhost:<<<__mysql_sandbox_port1>>>'.
You may want to kill these sessions to prevent them from performing unexpected updates:

1 open session(s) of 'root@localhost'.

Do you want to disable super_read_only and continue? [y/N]:
WARNING: Instance 'localhost:<<<__mysql_sandbox_port1>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
Creating InnoDB cluster 'sample' on 'localhost:<<<__mysql_sandbox_port1>>>'...

Adding Seed Instance...
Cluster successfully created. Use Cluster.addInstance() to add MySQL instances.
At least 3 instances are needed for the cluster to be able to withstand up to
one server failure.

//@<OUT> Adds a read only instance {VER(>=8.0.11)}
Validating instance at localhost:<<<__mysql_sandbox_port2>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.
A new instance will be added to the InnoDB cluster. Depending on the amount of
data on the cluster this might take from a few seconds to several hours.

Adding instance to the cluster ...

The instance 'localhost:<<<__mysql_sandbox_port2>>>' was successfully added to the cluster.

//@<OUT> Adds a read only instance {VER(<8.0.11)}
Validating instance at localhost:<<<__mysql_sandbox_port2>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.
WARNING: Instance 'localhost:<<<__mysql_sandbox_port2>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
A new instance will be added to the InnoDB cluster. Depending on the amount of
data on the cluster this might take from a few seconds to several hours.

Adding instance to the cluster ...

WARNING: Instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>' cannot persist configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
The instance 'localhost:<<<__mysql_sandbox_port2>>>' was successfully added to the cluster.

//@<OUT> Adds other instance {VER(>=8.0.11)}
Validating instance at localhost:<<<__mysql_sandbox_port3>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.
A new instance will be added to the InnoDB cluster. Depending on the amount of
data on the cluster this might take from a few seconds to several hours.

Adding instance to the cluster ...

The instance 'localhost:<<<__mysql_sandbox_port3>>>' was successfully added to the cluster.

//@<OUT> Adds other instance {VER(<8.0.11)}
Validating instance at localhost:<<<__mysql_sandbox_port3>>>...
NOTE: Instance detected as a sandbox.
Please note that sandbox instances are only suitable for deploying test clusters for use within the same host.

This instance reports its own address as <<<hostname>>>

Instance configuration is suitable.
WARNING: Instance 'localhost:<<<__mysql_sandbox_port3>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
A new instance will be added to the InnoDB cluster. Depending on the amount of
data on the cluster this might take from a few seconds to several hours.

Adding instance to the cluster ...

WARNING: Instance '<<<hostname>>>:<<<__mysql_sandbox_port1>>>' cannot persist configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
WARNING: Instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>' cannot persist configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
The instance 'localhost:<<<__mysql_sandbox_port3>>>' was successfully added to the cluster.

//@<OUT> Rejoins an instance
Rejoining the instance to the InnoDB cluster. Depending on the original
problem that made the instance unavailable, the rejoin operation might not be
successful and further manual steps will be needed to fix the underlying
problem.

Please monitor the output of the rejoin operation and take necessary action if
the instance cannot rejoin.

Rejoining instance to the cluster ...

<<<(__version_num<80011)?"WARNING: Instance 'localhost:"+__mysql_sandbox_port3+"' cannot persist Group Replication configuration since MySQL version "+__version+" does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.\n":""\>>>
The instance 'localhost:<<<__mysql_sandbox_port3>>>' was successfully rejoined on the cluster.

//@ persist GR configuration settings for 5.7 servers {VER(<8.0.11)}
||

//@ Stop sandbox 2
||

//@ Stop sandbox 3
||

//@ Stop sandbox 1
||

//@ Start sandbox 1
||

//@ Start sandbox 2
||

//@ Start sandbox 3
||

//@<OUT> Reboot the cluster {VER(>=8.0.11)}
Reconfiguring the cluster 'sample' from complete outage...

The instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>' was part of the cluster configuration.
Would you like to rejoin it to the cluster? [y/N]: 
The instance '<<<hostname>>>:<<<__mysql_sandbox_port3>>>' was part of the cluster configuration.
Would you like to rejoin it to the cluster? [y/N]: 
The MySQL instance at 'localhost:<<<__mysql_sandbox_port1>>>' currently has the super_read_only system
variable set to protect it from inadvertent updates from applications. You must
first unset it to be able to perform any changes to this instance.
For more information see:
https://dev.mysql.com/doc/refman/en/server-system-variables.html#sysvar_super_read_only.

NOTE: There are open sessions to 'localhost:<<<__mysql_sandbox_port1>>>'.
You may want to kill these sessions to prevent them from performing unexpected updates: 

1 open session(s) of 'root@localhost'. 

Do you want to disable super_read_only and continue? [y/N]: 

The cluster was successfully rebooted.

//@<OUT> Reboot the cluster {VER(<8.0.11)}
Reconfiguring the cluster 'sample' from complete outage...

The instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>' was part of the cluster configuration.
Would you like to rejoin it to the cluster? [y/N]:
The instance '<<<hostname>>>:<<<__mysql_sandbox_port3>>>' was part of the cluster configuration.
Would you like to rejoin it to the cluster? [y/N]:
The MySQL instance at 'localhost:<<<__mysql_sandbox_port1>>>' currently has the super_read_only system
variable set to protect it from inadvertent updates from applications. You must
first unset it to be able to perform any changes to this instance.
For more information see:
https://dev.mysql.com/doc/refman/en/server-system-variables.html#sysvar_super_read_only.

NOTE: There are open sessions to 'localhost:<<<__mysql_sandbox_port1>>>'.
You may want to kill these sessions to prevent them from performing unexpected updates:

1 open session(s) of 'root@localhost'.

Do you want to disable super_read_only and continue? [y/N]:
WARNING: Instance 'localhost:<<<__mysql_sandbox_port1>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
WARNING: Instance '<<<hostname>>>:<<<__mysql_sandbox_port2>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.
WARNING: Instance '<<<hostname>>>:<<<__mysql_sandbox_port3>>>' cannot persist Group Replication configuration since MySQL version <<<__version>>> does not support the SET PERSIST command (MySQL version >= 8.0.11 required). Please use the <Dba>.configureLocalInstance() command locally to persist the changes.

The cluster was successfully rebooted.

//@ Cleanup
||
