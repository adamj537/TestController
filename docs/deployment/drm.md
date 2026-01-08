# Deployment Readiness Matrix (DRM)
# Module: Embedded Tester Client (ESP32)

Last Updated: 2026-01-08

## Status Legend

- Current
- Incomplete
- Changing
- Final
- Draft
- Blocked
- Obsolete
- Review
- Approved

---

## FW-1 Firmware Source & Build System

- Summary: ESP32-DevKit tester client firmware with PlatformIO build system
- Status: Draft
- Repo References: embedded/tester-client/src/, embedded/tester-client/platformio.ini
- Objective: Complete buildable firmware with PlatformIO
- AI Context:
    files:
      - "embedded/tester-client/src/**/*.cpp"
      - "embedded/tester-client/src/**/*.h"
      - "embedded/tester-client/platformio.ini"
    description: |
      Review source organization, build configuration, dependencies
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"

## FW-2 API Reference Documentation

- Summary: Test controller public API for coordinator integration
- Status: Incomplete
- Repo References: embedded/tester-client/docs/api.md
- Objective: Documented API for HMI coordinator integration
- AI Context:
    files:
      - "embedded/tester-client/docs/api.md"
      - "embedded/tester-client/src/**/*.h"
    description: |
      Public functions, serial command interface, response formats
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"

## FW-3 Command Set Specification

- Summary: Serial/USB CDC command protocol for coordinator communication
- Status: Incomplete
- Repo References: embedded/tester-client/docs/command_spec.md
- Objective: Complete command protocol specification
- AI Context:
    files:
      - "embedded/tester-client/docs/command_spec.md"
    description: |
      Command format, parameters, responses, error codes for serial interface
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"

## FW-4 State Machine Documentation

- Summary: Test controller state machine (TC states)
- Status: Draft
- Repo References: embedded/tester-client/docs/state_machine.md, claude-context/11_state_tc.md
- Objective: Complete state machine documentation
- AI Context:
    files:
      - "embedded/tester-client/docs/state_machine.md"
      - "claude-context/11_state_tc.md"
    description: |
      TC states, transitions, event handling
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"

## FW-5 Error Codes & Handling

- Summary: Error code definitions for test controller
- Status: Draft
- Repo References: embedded/tester-client/docs/error_codes.md, claude-context/06_error_matrix.md
- Objective: Standardized error reporting
- AI Context:
    files:
      - "embedded/tester-client/docs/error_codes.md"
      - "claude-context/06_error_matrix.md"
    description: |
      Error code definitions, recovery procedures
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"

## FW-6 Memory Layout & Configuration

- Summary: Flash/RAM usage, ESP32 partition scheme
- Status: Incomplete
- Repo References: embedded/tester-client/docs/memory_layout.md
- Objective: Memory requirements documentation

## FW-7 Test Coverage & Results

- Summary: Unit tests for tester firmware
- Status: Incomplete
- Repo References: embedded/tester-client/test/
- Objective: Test coverage for critical firmware functions

## FW-8 Integration Guide

- Summary: Integration instructions for tester firmware with hardware and coordinator
- Status: Incomplete
- Repo References: embedded/tester-client/docs/integration.md
- Objective: Step-by-step integration guide

## MQTT-1 MQTT/Sparkplug B Integration

- Summary: MQTT Sparkplug B topics (SensitMfg/G3-MB-Tester-{serial})
- Status: Draft
- Repo References: claude-context/04_mqtt_contract.md
- Objective: Sparkplug B compliance for remote monitoring
- AI Context:
    files:
      - "claude-context/04_mqtt_contract.md"
    description: |
      MQTT topic structure, Sparkplug B payload format, NBIRTH/NDEATH, DDATA, DCMD
    required_outputs:
      - "Summary"
      - "Status recommendation"
    forbidden_outputs:
      - "Automatic code changes"
