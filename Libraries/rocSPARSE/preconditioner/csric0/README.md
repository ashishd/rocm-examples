# rocSPARSE Preconditioner CSR Incomplete Cholesky Decomposition Example

## Description
This example illustrates the use of the `rocSPARSE` incomplete Cholesky factorization preconditioner using the CSR storage format.

Given a Hermitian and [positive definite](https://en.wikipedia.org/wiki/Definite_matrix) matrix $A$, computing its Cholesky decomposition consists of finding a lower triangular matrix $L$ such that
$$A = L \cdot L^H,$$

where $L^H$ denotes the Hermitian/conjugate transpose of matrix $L$.

The _incomplete_ Cholesky decomposition is a sparse approximation of the above-mentioned Cholesky decomposition. Thus, `rocSPARSE` allows us to compute a sparse lower triangular matrix $L$ such that
$$A \approx L \cdot L^H.$$

### Application flow
1. Set up input data.
2. Allocate device memory and offload input data to the device.
3. Initialize rocSPARSE by creating a handle.
4. Prepare utility variables for rocSPARSE csric0 invocation.
5. Perform the analysis step.
6. Call dcsric0 to perform incomplete Cholesky factorization.
7. Check zero-pivots.
8. Convert the resulting CSR sparse matrix to a dense matrix. Check and print the resulting matrix.
9. Free rocSPARSE resources and device memory.
10. Print validation result.

## Key APIs and Concepts
### CSR Matrix Storage Format
The [Compressed Sparse Row (CSR) storage format](https://rocsparse.readthedocs.io/en/latest/usermanual.html#csr-storage-format) describes an $m \times n$ sparse matrix with three arrays.

Defining
- `m`: number of rows
- `n`: number of columns
- `nnz`: number of non-zero elements

we can describe a sparse matrix using the following arrays:
- `csr_val`: array storing the non-zero elements of the matrix.
- `csr_row_ptr`: given $i \in [0, m]$
    - if $` 0 \leq i < m `$, `csr_row_ptr[i]` stores the index of the first non-zero element in row $i$ of the matrix
    - if $i = m$, `csr_row_ptr[i]` stores `nnz`.

    This way, row $j \in [0, m)$ contains the non-zero elements of indices from `csr_row_ptr[j]` to `csr_row_ptr[j+1]-1`. Therefore, the corresponding values in `csr_val` can be accessed from `csr_row_ptr[j]` to `csr_row_ptr[j+1]-1`.
- `csr_col_ind`: given $i \in [0, nnz-1]$, `csr_col_ind[i]` stores the column of the $i^{th}$ non-zero element in the matrix.

The CSR matrix is sorted by column indices in the same row, and each pair of indices appear only once.

For instance, consider a sparse matrix as

$$
A=
\left(
\begin{array}{ccccc}
1 & 2 & 0 & 3 & 0 \\
0 & 4 & 5 & 0 & 0 \\
6 & 0 & 0 & 7 & 8
\end{array}
\right)
$$

Therefore, the CSR representation of $A$ is:

```
m = 3

n = 5

nnz = 8

csr_val = { 1, 2, 3, 4, 5, 6, 7, 8 }

csr_row_ptr = { 0, 3, 5, 8 }

csr_col_ind = { 0, 1, 3, 1, 2, 0, 3, 4 }
```

### rocSPARSE
- rocSPARSE is initialized by calling `rocsparse_create_handle(rocsparse_handle*)` and is terminated by calling `rocsparse_destroy_handle(rocsparse_handle)`.
- `rocsparse_mat_descr descr`: holds all properties of a matrix. The properties set in this example are the following:
    - `rocsparse_fill_mode`: indicates whether a (triangular) matrix is lower (`rocsparse_fill_mode_lower`) or upper (`rocsparse_fill_mode_upper`) triangular.
- `rocsparse_solve_policy policy`: specifies the policy to follow for triangular solvers and factorizations. The only value accepted is `rocsparse_solve_policy_auto`.
- `rocsparse_analysis_policy analysis`: specifies the policy to follow for analysis data. The following values are accepted:
    - `rocsparse_analysis_policy_reuse`: the analysis data gathered is re-used.
    - `rocsparse_analysis_policy_force`: the analysis data will be re-built.
- `rocsparse_[sdcz]csric0` computes the incomplete Cholesky factorization of a sparse CSR matrix $A$, such that $A \approx L \cdot L^H$. The correct function signature should be chosen based on the datatype of the input matrix:
    - `s` single-precision real (`float`)
    - `d` double-precision real (`double`)
    - `c` single-precision complex (`rocsparse_float_complex`)
    - `z` double-precision complex (`rocsparse_double_complex`)
- `rocsparse_[sdcz]csric0_analysis` performs the analysis step for `rocsparse_[sdcz]csric0`. The character matched in `[sdcz]` coincides with the one matched in `rocsparse_[sdcz]csric0`.
- `rocsparse_[sdcz]csric0_buffer_size` allows to obtain the size (in bytes) of the temporary storage buffer required for the `rocsparse_[sdcz]csric0_analysis` and `rocsparse_[sdcz]csric0` functions. The character matched in `[sdcz]` coincides with the one matched in any of the mentioned functions.
- `rocsparse_csric0_zero_pivot(rocsparse_handle, rocsparse_mat_info, rocsparse_int *position)` returns `rocsparse_status_zero_pivot` if either a structural or numerical zero has been found during the execution of `rocsparse_[sbcz]csric0(....)` and stores in `position` the index $i$ of the first zero pivot $A_{ii}$ found. If no zero pivot is found it returns `rocsparse_status_success`.

## Demonstrated API Calls

### rocSPARSE
- `rocsparse_analysis_policy`
- `rocsparse_analysis_policy_reuse`
- `rocsparse_csric0_zero_pivot`
- `rocsparse_create_handle`
- `rocsparse_create_mat_descr`
- `rocsparse_create_mat_info`
- `rocsparse_dcsric0`
- `rocsparse_dcsric0_analysis`
- `rocsparse_dcsric0_buffer_size`
- `rocsparse_dcsr2dense`
- `rocsparse_destroy_handle`
- `rocsparse_destroy_mat_descr`
- `rocsparse_destroy_mat_info`
- `rocsparse_fill_mode_lower`
- `rocsparse_handle`
- `rocsparse_int`
- `rocsparse_mat_descr`
- `rocsparse_mat_info`
- `rocsparse_set_mat_fill_mode`
- `rocsparse_solve_policy`
- `rocsparse_solve_policy_auto`
- `rocsparse_status`
- `rocsparse_status_zero_pivot`

### HIP runtime
- `hipFree`
- `hipMalloc`
- `hipMemcpy`
- `hipMemcpyDeviceToHost`
- `hipMemcpyHostToDevice`