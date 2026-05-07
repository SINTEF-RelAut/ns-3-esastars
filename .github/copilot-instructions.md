# GitHub Copilot Instructions: ns-3 SCION/BGP Simulation

## Core Context
- **Project**: Network simulation extension for ns-3 for simulation of SCION and BGP protocols between satellite constellations and ground stations.
- **Goal**: Implementing and testing SCION and BGP protocol behaviors and evaluate how effective they are in different scenarios, with dedicated IXP satellites and with direct links. Main metrics will be routing convergence time, resilience to link failures, packet loss, and latency.
- **Languages**: C++ (core logic, headers) and Python (bindings, orchestration, scripts).
- **Environment**: ns-3 framework (strict adherence to its memory management and smart pointers).

## Coding Standards
- **C++**: 
    - Follow ns-3 coding style (CamelCase for functions, PascalCase for classes).
    - Use `Ptr<T>` for ns-3 objects instead of raw pointers or `std::shared_ptr`.
    - Use `NS_LOG_COMPONENT_DEFINE` and `NS_LOG` macros for debugging.
    - Always include necessary header guards and Doxygen blocks.
- **Python**:
    - Follow PEP 8.
    - Use type hints for all function signatures.
    - Ensure compatibility with ns-3 Python bindings (`cppyy`).

## Documentation Requirements
- All C++ headers must include Doxygen comments:
    - `\brief` for short descriptions.
    - `\param` for every input.
    - `\return` for return values.
    - `\note` for simulation-specific constraints.
- Python scripts must include docstrings following the Google style.

## Automated Testing & Quality
- **Unit Tests**: 
    - C++: Create tests inheriting from `ns3::TestCase`.
    - Python: Use `pytest` for orchestration scripts.
- **Verification**: 
    - Check for common networking pitfalls (propagation delay, packet drops).
    - When generating BGP or SCION logic, verify path selection algorithms against RFC/spec standards.

## Interaction Rules
- **Conciseness**: Give me the code first, then a brief explanation of the logic.
- **Efficiency**: If I ask for a protocol modification, suggest the most performant ns-3 helper or attribute approach.
- **Error Handling**: Always include `NS_ASSERT` or `NS_ABORT_MSG` for critical simulation states.
