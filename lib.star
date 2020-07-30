waterfall_acls = [
    acl.entry(
            roles=acl.BUILDBUCKET_TRIGGERER,
            users=[
              'luci-scheduler@appspot.gserviceaccount.com',
              'v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com',
            ],
            groups=['service-account-v8-bot']
        ),
]

tryserver_acls = [
    acl.entry(
            roles=acl.BUILDBUCKET_TRIGGERER,
            users=[
              'luci-scheduler@appspot.gserviceaccount.com',
            ],
            groups=[
              'service-account-cq',
              'project-v8-tryjob-access',
              'service-account-v8-bot',
            ]
        ),
]