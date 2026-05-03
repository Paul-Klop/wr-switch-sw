# PTP quality SNMP readout for White Rabbit Switch v8.0

## Summary

This branch adds read-only SNMP telemetry fields for PTP clock-quality information.

It exposes:

- selected grandmaster clockClass
- PTP stepsRemoved / hop count

## New SNMP fields

Two read-only fields were added to `wrsPtpDataTable`.

| Field | OID | Source | Meaning |
|---|---|---|---|
| `wrsPtpClockClass` | `1.3.6.1.4.1.96.100.7.5.1.34` | `ppsi_parentDS->grandmasterClockQuality.clockClass` | selected grandmaster clockClass |
| `wrsPtpStepsRemoved` | `1.3.6.1.4.1.96.100.7.5.1.35` | `ppsi_currentDS->stepsRemoved` | PTP stepsRemoved / hop count to selected grandmaster |
