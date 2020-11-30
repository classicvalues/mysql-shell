//@ Initialization
||

//@<OUT> create cluster
{
    "clusterName": "dev",
    "defaultReplicaSet": {
        "name": "default",
        "primary": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
        "ssl": "REQUIRED",
        "status": "OK_NO_TOLERANCE",
        "statusText": "Cluster is NOT tolerant to any failures.",
        "topology": {
            "<<<hostname>>>:<<<__mysql_sandbox_port1>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
                "mode": "R/W",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            }
        },
        "topologyMode": "Single-Primary"
    },
    "groupInformationSourceMember": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>"
}

//@ Add instance 2
||

//@ Add instance 3
||

//@<OUT> forceQuorumUsingPartitionOf() must not be allowed on cluster with quorum
ERROR: Cannot perform operation on an healthy cluster because it can only be used to restore a cluster from quorum loss.

//@<ERR> forceQuorumUsingPartitionOf() must not be allowed on cluster with quorum
Cluster.forceQuorumUsingPartitionOf: The cluster has quorum according to instance 'localhost:<<<__mysql_sandbox_port1>>>' (RuntimeError)

//@ Disable group_replication_start_on_boot on second instance {VER(>=8.0.11)}
||

//@ Disable group_replication_start_on_boot on second instance {VER(<8.0.11)}
||

//@ Disable group_replication_start_on_boot on third instance {VER(>=8.0.11)}
||

//@ Disable group_replication_start_on_boot on third instance {VER(<8.0.11)}
||

//@ Kill instance 2
||

//@ Kill instance 3
||

//@ Start instance 2
||

//@ Start instance 3
||

//@<OUT> Cluster status
{
    "clusterName": "dev",
    "defaultReplicaSet": {
        "name": "default",
        "primary": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
        "ssl": "REQUIRED",
        "status": "NO_QUORUM",
        "statusText": "Cluster has no quorum as visible from '<<<hostname>>>:<<<__mysql_sandbox_port1>>>' and cannot process write transactions. 2 members are not active.",
        "topology": {
            "<<<hostname>>>:<<<__mysql_sandbox_port1>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
                "mode": "R/O",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port2>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port2>>>",
                "instanceErrors": [
                    "NOTE: group_replication is stopped."
                ],
                "memberState": "OFFLINE",
                "mode": "R/O",
                "readReplicas": {},
                "role": "HA",
                "status": "(MISSING)",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port3>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port3>>>",
                "instanceErrors": [
                    "NOTE: group_replication is stopped."
                ],
                "memberState": "OFFLINE", 
                "mode": "R/O",
                "readReplicas": {},
                "role": "HA",
                "status": "UNREACHABLE",
                "version": "[[*]]"
            }
        },
        "topologyMode": "Single-Primary"
    },
    "groupInformationSourceMember": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>"
}

//@ Disconnect and reconnect to instance
||

//@<OUT> Get cluster operation must show a warning because there is no quorum
WARNING: Cluster has no quorum and cannot process write transactions: Group has no quorum

//@ Cluster.forceQuorumUsingPartitionOf errors
||Cluster.forceQuorumUsingPartitionOf: Invalid number of arguments, expected 1 to 2 but got 0
||Cluster.forceQuorumUsingPartitionOf: Argument #1: Invalid connection options, expected either a URI or a Connection Options Dictionary
||Cluster.forceQuorumUsingPartitionOf: Argument #1: Invalid URI: empty.
||Cluster.forceQuorumUsingPartitionOf: Argument #1: Invalid connection options, expected either a URI or a Connection Options Dictionary
||Cluster.forceQuorumUsingPartitionOf: The instance 'localhost:<<<__mysql_sandbox_port2>>>' cannot be used to restore the cluster as it is not an active member of replication group.


//@ Cluster.forceQuorumUsingPartitionOf success
||

//@<OUT> Cluster status after force quorum
{
    "clusterName": "dev",
    "defaultReplicaSet": {
        "name": "default",
        "primary": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
        "ssl": "REQUIRED",
        "status": "OK_NO_TOLERANCE",
        "statusText": "Cluster is NOT tolerant to any failures. 2 members are not active.",
        "topology": {
            "<<<hostname>>>:<<<__mysql_sandbox_port1>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
                "mode": "R/W",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port2>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port2>>>",
                "instanceErrors": [
                    "NOTE: group_replication is stopped."
                ],
                "memberState": "OFFLINE",
                "mode": "R/O",
                "readReplicas": {},
                "role": "HA",
                "status": "(MISSING)",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port3>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port3>>>",
                "instanceErrors": [
                    "NOTE: group_replication is stopped."
                ],
                "memberState": "OFFLINE",
                "mode": "R/O",
                "readReplicas": {},
                "role": "HA",
                "status": "(MISSING)",
                "version": "[[*]]"
            }
        },
        "topologyMode": "Single-Primary"
    },
    "groupInformationSourceMember": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>"
}

//@ Rejoin instance 2
||

//@ Rejoin instance 3
||

//@<OUT> Cluster status after rejoins
{
    "clusterName": "dev",
    "defaultReplicaSet": {
        "name": "default",
        "primary": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
        "ssl": "REQUIRED",
        "status": "OK",
        "statusText": "Cluster is ONLINE and can tolerate up to ONE failure.",
        "topology": {
            "<<<hostname>>>:<<<__mysql_sandbox_port1>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>",
                "mode": "R/W",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port2>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port2>>>",
                "mode": "R/O",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            },
            "<<<hostname>>>:<<<__mysql_sandbox_port3>>>": {
                "address": "<<<hostname>>>:<<<__mysql_sandbox_port3>>>",
                "mode": "R/O",
                "readReplicas": {},<<<(__version_num>=80011) ?  "\n                \"replicationLag\": [[*]],":"">>>
                "role": "HA",
                "status": "ONLINE",
                "version": "[[*]]"
            }
        },
        "topologyMode": "Single-Primary"
    },
    "groupInformationSourceMember": "<<<hostname>>>:<<<__mysql_sandbox_port1>>>"
}

//@ STOP group_replication on instance where forceQuorumUsingPartitionOf() was executed.
||

//@ Start group_replication on instance the same instance succeeds because group_replication_force_members is empty.
||

//@<OUT> BUG#30739252: Confirm instance 3 is never included as OFFLINE (no undefined behaviour) {VER(>=8.0.16)}
CHANNEL_NAME	MEMBER_ID	MEMBER_HOST	MEMBER_PORT	MEMBER_STATE	MEMBER_ROLE	MEMBER_VERSION
group_replication_applier	[[*]]	<<<hostname>>>	<<<__mysql_sandbox_port2>>>	ONLINE	PRIMARY	<<<__version>>>

//@ Finalization
||
