---
title: Create a KMS configuration using Equinix SmartKey
headerTitle: Create a KMS configuration using Equinix SmartKey
linkTitle: Create a KMS configuration
description: Use Yugabyte Platform to create a KMS configuration using Equinix SmartKey.
menu:
  latest:
    parent: security
    identifier: create-kms-config-2-smartkey-kms
    weight: 27
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./aws-kms" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      AWS KMS
    </a>
  </li>

  <li >
    <a href="{{< relref "./equinix-smartkey" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      Equinix SmartKey
    </a>
  </li>

  <li >
    <a href="{{< relref "./hashicorp-kms.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      HashiCorp Vault
    </a>
  </li>

</ul>

Encryption at rest uses universe keys to encrypt and decrypt universe data keys. You can use the Yugabyte Platform console to create key management service (KMS) configurations for generating the required universe keys for one or more YugabyteDB universes. Encryption at rest in Yugabyte Platform supports the use of [Equinix SmartKey](https://www.equinix.com/services/digital-infrastructure-services/smartkey) as a KMS.

You can create a KMS configuration with Equinix SmartKey as follows:

1. Open the Yugabyte Platform console and navigate to **Configs > Security > Encryption At Rest**. A list of existing configurations appears.
2. Click **Create New Config**.
3. Enter the following configuration details in the form:
    - **Configuration Name** — Enter a meaningful name for your configuration.
    - **KMS Provider** — Select **Equinix SmartKey**.
    - **API Url** — Enter the URL for the API. The default is `api.amer.smartkey.io`.
    - **Secret API Key** — Enter the secret API key.
4. Click **Save**. The new KMS configuration should appear in the list of existing configurations. A saved KMS configuration can only be deleted if it is not in use by any existing universes.
5. Optionally, to confirm the information is correct, click **Show details**. Note that sensitive configuration values are displayed partially masked.

