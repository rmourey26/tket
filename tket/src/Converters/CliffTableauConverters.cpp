// Copyright 2019-2022 Cambridge Quantum Computing
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdexcept>

#include "Converters.hpp"

namespace tket {

CliffTableau circuit_to_tableau(const Circuit &circ) {
  CliffTableau tab(circ.all_qubits());
  for (const Command &com : circ) {
    std::vector<unsigned> qbs;
    for (const UnitID &qb : com.get_args()) {
      qbs.push_back(tab.qubits_.left.at(Qubit(qb)));
    }
    tab.apply_gate_at_end(com.get_op_ptr()->get_type(), qbs);
  }
  return tab;
}

Circuit tableau_to_circuit(const CliffTableau &tab) {
  CliffTableau tabl(tab);
  unsigned size = tabl.size_;
  /*
   * Aaronson-Gottesman: Improved Simulation of Stabilizer Circuits, Theorem 8
   * Any unitary stabilizer circuit has an equivalent circuit in canonical form
   * (H-C-P-C-P-C-H-P-C-P-C).
   * Produces a circuit realising the tableau, but consumes the tableau in the
   * process.
   */
  Circuit c(size);

  /*
   * Step 1: Use Hadamards (in our case, Vs) to make C (zpauli_x) have full rank
   */
  MatrixXb echelon = tabl.zpauli_x;
  std::map<unsigned, unsigned> leading_val_to_col;
  for (unsigned i = 0; i < size; i++) {
    for (unsigned j = 0; j < size; j++) {
      if (echelon(j, i)) {
        if (leading_val_to_col.find(j) == leading_val_to_col.end()) {
          leading_val_to_col.insert({j, i});
          break;
        } else {
          unsigned l = leading_val_to_col.at(j);
          for (unsigned k = 0; k < size; k++) {
            echelon(k, i) ^= echelon(k, l);
          }
        }
      }
    }
    if (leading_val_to_col.size() > i)
      continue;  // Independent of previous cols
    c.add_op<unsigned>(OpType::V, {i});
    tabl.apply_V_at_front(i);
    tabl.apply_V_at_front(i);
    tabl.apply_V_at_front(i);
    echelon.col(i) = tabl.zpauli_z.col(i);
    for (unsigned j = 0; j < size; j++) {
      if (echelon(j, i)) {
        if (leading_val_to_col.find(j) == leading_val_to_col.end()) {
          leading_val_to_col.insert({j, i});
          break;
        } else {
          unsigned l = leading_val_to_col.at(j);
          for (unsigned k = 0; k < size; k++) {
            echelon(k, i) ^= echelon(k, l);
          }
        }
      }
    }
    if (leading_val_to_col.size() == i)
      throw std::invalid_argument("Stabilisers are not mutually independent");
  }

  /*
   * Step 2: Use CXs to perform Gaussian elimination on C (zpauli_x), producing
   * / A B \
   * \ I D /
   */
  for (const std::pair<unsigned, unsigned> &qbs :
       gaussian_elimination_col_ops(tabl.zpauli_x)) {
    c.add_op<unsigned>(OpType::CX, {qbs.first, qbs.second});
    tabl.apply_CX_at_front(qbs.first, qbs.second);
  }

  /*
   * Step 3: Commutativity of the stabilizer implies that ID^T is symmetric,
   * therefore D is symmetric, and we can apply phase (S) gates to add a
   * diagonal matrix to D and use Lemma 7 to convert D to the form D = MM^T
   * for some invertible M.
   */
  std::pair<MatrixXb, MatrixXb> zp_z_llt =
      binary_LLT_decomposition(tabl.zpauli_z);
  for (unsigned i = 0; i < size; i++) {
    if (zp_z_llt.second(i, i)) {
      c.add_op<unsigned>(OpType::S, {i});
      tabl.apply_S_at_front(i);
      tabl.apply_S_at_front(i);
      tabl.apply_S_at_front(i);
    }
  }

  /*
   * Step 4: Use CXs to produce
   * / A B \
   * \ M M /
   * Note that when we map I to IM, we also map D to D(M^T)^{-1} = M.
   */
  std::vector<std::pair<unsigned, unsigned>> m_to_i =
      gaussian_elimination_col_ops(zp_z_llt.first);
  for (std::vector<std::pair<unsigned, unsigned>>::reverse_iterator it =
           m_to_i.rbegin();
       it != m_to_i.rend(); it++) {
    c.add_op<unsigned>(OpType::CX, {it->first, it->second});
    tabl.apply_CX_at_front(it->first, it->second);
  }

  /*
   * Step 5: Apply phases to all n qubits to obtain
   * / A B \
   * \ M 0 /
   * Since M is full rank, there exists some subset S of qubits such that
   * applying two phases in succession (Z) to every a \in S will preserve
   * the tableau, but set r_{n+1} = ... = r_{2n} = 0 (zpauli_phase = 0^n).
   * Apply two phases (Z) to every a \in S. DELAYED UNTIL END
   */
  for (unsigned i = 0; i < size; i++) {
    c.add_op<unsigned>(OpType::S, {i});
    tabl.apply_S_at_front(i);
    tabl.apply_S_at_front(i);
    tabl.apply_S_at_front(i);
  }

  /*
   * Step 6: Use CXs to perform Gaussian elimination on M, producing
   * / A B \
   * \ I 0 /
   * By commutativity relations, IB^T = A0^T + I, therefore B = I.
   */
  for (const std::pair<unsigned, unsigned> &qbs :
       gaussian_elimination_col_ops(tabl.zpauli_x)) {
    c.add_op<unsigned>(OpType::CX, {qbs.first, qbs.second});
    tabl.apply_CX_at_front(qbs.first, qbs.second);
  }

  /*
   * Step 7: Use Hadamards to produce
   * / I A \
   * \ 0 I /
   */
  for (unsigned i = 0; i < size; i++) {
    c.add_op<unsigned>(OpType::H, {i});
    tabl.apply_S_at_front(i);
    tabl.apply_V_at_front(i);
    tabl.apply_S_at_front(i);
  }

  /*
   * Step 8: Now commutativity of the destabilizer implies that A is symmetric,
   * therefore we can again use phase (S) gates and Lemma 7 to make A = NN^T for
   * some invertible N.
   */
  std::pair<MatrixXb, MatrixXb> xp_z_llt =
      binary_LLT_decomposition(tabl.xpauli_z);
  for (unsigned i = 0; i < size; i++) {
    if (xp_z_llt.second(i, i)) {
      c.add_op<unsigned>(OpType::S, {i});
      tabl.apply_S_at_front(i);
      tabl.apply_S_at_front(i);
      tabl.apply_S_at_front(i);
    }
  }

  /*
   * Step 9: Use CXs to produce
   * / N N \
   * \ 0 C /
   */
  std::vector<std::pair<unsigned, unsigned>> n_to_i =
      gaussian_elimination_col_ops(xp_z_llt.first);
  for (std::vector<std::pair<unsigned, unsigned>>::reverse_iterator it =
           n_to_i.rbegin();
       it != n_to_i.rend(); it++) {
    c.add_op<unsigned>(OpType::CX, {it->first, it->second});
    tabl.apply_CX_at_front(it->first, it->second);
  }

  /*
   * Step 10: Use phases (S) to produce
   * / N 0 \
   * \ 0 C /
   * then by commutativity relations NC^T = I. Next apply two phases (Z) each to
   * some subset of qubits in order to preserve the above tableau, but set
   * r_1 = ... = r_n = 0 (xpauli_phase = 0^n). DELAYED UNTIL END
   */
  for (unsigned i = 0; i < size; i++) {
    c.add_op<unsigned>(OpType::S, {i});
    tabl.apply_S_at_front(i);
    tabl.apply_S_at_front(i);
    tabl.apply_S_at_front(i);
  }

  /*
   * Step 11: Use CXs to produce
   * / I 0 \
   * \ 0 I /
   */
  for (const std::pair<unsigned, unsigned> &qbs :
       gaussian_elimination_col_ops(tabl.xpauli_x)) {
    c.add_op<unsigned>(OpType::CX, {qbs.first, qbs.second});
    tabl.apply_CX_at_front(qbs.first, qbs.second);
  }

  /*
   * DELAYED STEPS: Set all phases to 0 by applying Z or X gates
   */
  for (unsigned i = 0; i < size; i++) {
    if (tabl.xpauli_phase(i)) {
      c.add_op<unsigned>(OpType::Z, {i});
      tabl.apply_S_at_front(i);
      tabl.apply_S_at_front(i);
    }
    if (tabl.zpauli_phase(i)) {
      c.add_op<unsigned>(OpType::X, {i});
      tabl.apply_V_at_front(i);
      tabl.apply_V_at_front(i);
    }
  }

  /*
   * Rename qubits
   */
  unit_map_t rename_map;
  for (boost::bimap<Qubit, unsigned>::iterator iter = tabl.qubits_.begin(),
                                               iend = tabl.qubits_.end();
       iter != iend; ++iter) {
    rename_map.insert({Qubit(q_default_reg(), iter->right), iter->left});
  }
  c.rename_units(rename_map);

  return c;
}

}  // namespace tket
