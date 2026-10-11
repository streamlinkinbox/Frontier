#Requires -Version 5.1
<#
.SYNOPSIS
  Creates an Xsolla SANDBOX payment token for a Charge catalog item, then
  prints and opens the test checkout URL. No real money moves.
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File XsollaSandboxToken.ps1 -ApiKey "PASTE_KEY_HERE"
  powershell -ExecutionPolicy Bypass -File XsollaSandboxToken.ps1 -ApiKey "PASTE_KEY_HERE" -Sku premium_pass
#>
param(
  [Parameter(Mandatory = $true)][string]$ApiKey,
  [string]$Sku = "entry_tokens",
  [int]$Quantity = 1
)

$ErrorActionPreference = "Stop"
$merchantId = "448257"
$projectId = "317410"

$auth = [Convert]::ToBase64String(
  [Text.Encoding]::ASCII.GetBytes("${merchantId}:${ApiKey}"))

$body = @{
  sandbox  = $true
  user     = @{
    id      = @{ value = "sandbox-tester-1" }
    name    = @{ value = "Sandbox Tester" }
    email   = @{ value = "sandbox-tester@example.com" }
    country = @{ value = "US"; allow_modify = $true }
  }
  purchase = @{ items = @( @{ sku = $Sku; quantity = $Quantity } ) }
  settings = @{ language = "en"; currency = "USD" }
} | ConvertTo-Json -Depth 6

$result = Invoke-RestMethod `
  -Uri "https://store.xsolla.com/api/v3/project/${projectId}/admin/payment/token" `
  -Method Post `
  -Headers @{ Authorization = "Basic $auth" } `
  -ContentType "application/json" `
  -Body $body

$url = "https://sandbox-secure.xsolla.com/paystation4/?token=$($result.token)"
Write-Host ""
Write-Host "Order ID : $($result.order_id)"
Write-Host "Checkout : $url"
Write-Host ""
Write-Host "Pay with test Visa 4111 1111 1111 1111, exp 12/40, any CVV, ZIP 12345."
Start-Process $url
