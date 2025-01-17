pytket.qasm
==================================
:py:class:`Circuit` objects can be converted to and from OpenQASM, although we do not support all operations.
In particular, we do not currently support:

1) Importing from libraries other than "qelib1"
2) Ability to interpret gates acting on a whole register in the OpenQASM style

Any pytket :py:class:`Circuit` that is exported to this format should be valid for importing again as a :py:class:`Circuit`, making this a convenient file format
to save your :py:class:`Circuit` objects.

However, we do support symbolic parameters of gates, both on import and export.

.. note:: Unlke pytket backends the qasm converters do not handle `implicit qubit permutations <https://cqcl.github.io/pytket/manual/manual_circuit.html#implicit-qubit-permutations>`_ . In other words if a circuit containing an implicit qubit permutation is converted to a qasm file the implicit permutation will not be accounted for and the circuit will be missing this permutation when reimported.

.. automodule:: pytket.qasm
    :members: circuit_from_qasm, circuit_from_qasm_wasm, circuit_to_qasm, circuit_from_qasm_str, circuit_to_qasm_str, circuit_from_qasm_io, circuit_to_qasm_io
