#ifndef _DATACONSOLIDATOR_H_
#define _DATACONSOLIDATOR_H_

#include "Result.h"

extern Logger* logger;
/**
 * Impute missing genotype (<0) according to population frequency (p^2, 2pq, q^2)
 */
void imputeGenotypeByFrequency(Matrix* genotype, Random* r) {
  Matrix& m = *genotype;
  for (int i = 0; i < m.cols; i++ ) {
    int ac = 0;
    int an = 0;
    for (int j = 0; j < m.rows; j++) {
      if (m[j][i] >= 0) {
        ac += m[j][i];
        an += 2;
      }
    }
    double p = 1.0 * ac / an;
    double pRef = p * p;
    double pHet = pRef + 2.0*p * (1.0 - p);
    for (int j = 0; j < m.rows; j++){
      if (m[j][i] < 0) {
        double v = r->Next();
        if (v < pRef) {
          m[j][i] = 0;
        } else if (v < pHet) {
          m[j][i] = 1;
        } else {
          m[j][i] = 2;
        }
      }
    }
  }
};

/**
 * Impute missing genotype (<0) according to its mean genotype
 * @param genotype (people by marker matrix)
 */
void imputeGenotypeToMean(Matrix* genotype) {
  Matrix& m = *genotype;
  for (int i = 0; i < m.cols; i++ ) {
    int ac = 0;
    int an = 0;
    for (int j = 0; j < m.rows; j++) {
      if (m[j][i] >= 0) {
        ac += m[j][i];
        an += 2;
      }
    }
    double p = 1.0 * ac / an;
    for (int j = 0; j < m.rows; j++){
      if (m[j][i] < 0) {
        m[j][i] = p;
      }
    }
  }
};

/**
 * @return true if any of the markers (@param col) of @param genotype (people by marker) is missing
 */
bool hasMissingMarker(Matrix& genotype, int col) {
  if (col >= genotype.cols || col < 0) {
    logger->error("Invalid check of missing marker.");
    return false;
  }

  for (int r = 0; r < genotype.rows; ++r) {
    if (genotype[r][col] < 0)
      return true;
  }
  return false;
};

/**
 * Remove columns of markers in @param genotype (people by marker) where there are missing genotypes
 */
void removeMissingMarker(Matrix* genotype) {
  Matrix& g = *genotype;
  int col = 0;
  while (col < g.cols) {
    if (hasMissingMarker(g, col)) {
      // move last column to this column
      const int lastCol = g.cols - 1 ;
      for (int r = 0; r < g.rows; ++r){
        g[r][col] = g[r][lastCol];
      }
      g.SetColumnLabel(col, g.GetColumnLabel(lastCol));
      g.Dimension(g.rows, lastCol);
      continue;
    };
    ++ col;
  };
};
/**
 * @return true if markers on @param col of @param genotype (people by marker) is monomorphic (genotypes are all the same)
 */
bool isMonomorphicMarker(Matrix& genotype, int col) {
  if (col >= genotype.cols || col < 0) {
    logger->error("Invalid check of monomorhpic marker.");
    return false;
  }

  // first first non-missing genotype
  int nonMissingRow = genotype.rows;
  for (int i = 0; i < genotype.rows; ++i) {
    if (genotype[i][col] >= 0) {
      nonMissingRow = i;
      break;
    }
  }

  for (int r = nonMissingRow + 1; r < genotype.rows; ++r) {
    if (genotype[r][col] < 0) // missing
      continue;
    if (genotype[r][col] != genotype[nonMissingRow][col])
      return false;
  }
  return true;
};

/**
 * remove monomorphic columns of @param genotype
 */
void removeMonomorphicSite(Matrix* genotype) {
  Matrix& g = *genotype;
  int col = 0;
  while (col < g.cols) {
    if (isMonomorphicMarker(g, col)) {
      // move last column to this column
      const int lastCol = g.cols - 1 ;
      for (int r = 0; r < g.rows; ++r){
        g[r][col] = g[r][lastCol];
      }
      g.Dimension(g.rows, lastCol);
      continue;
    };
    ++ col;
  };
};

/**
 * This class is charge to cleanning data before fitting in model
 * The cleaning step includes:
 *  remove monomorphic sites
 *  handle missing data genotype (impute to mean, impute by HWE, filter out and its corresponding phenotypes, covariates)
 *  (future) include weights (GERP, Sift)
 *  (future) re-weight genotype (dominate model, recessive model)
 *  (future) take-in kinship matrix
 */
class DataConsolidator{
public:
  const static int UNINITIALIZED = 0;
  const static int IMPUTE_MEAN = 1;
  const static int IMPUTE_HWE = 2;
  const static int DROP = 3;
DataConsolidator(): strategy(UNINITIALIZED) {
  };
  void setStrategy(const int s){
    this->strategy = s;
  };
  /**
   * @param pheno, @param cov @param genotype are all ordered and sorted by the same people
   * @param phenoOut, @param covOut and @param genotype are outputted
   * NOTE: @param covOut may be filled as column vector of 1 if @param cov is empty
   */
  void consolidate(Matrix& pheno, Matrix& cov, Matrix& geno) {
    this->originalGenotype = geno;
    this->genotype = geno;

    // remove monomorphic site
    removeMonomorphicSite(&genotype);

    copyColName(pheno, &this->phenotype);
    copyColName(cov,   &this->covariate);
    if (this->strategy == IMPUTE_MEAN) {
      // impute missing genotypes
      imputeGenotypeToMean(&this->genotype);
      /* *phenoOut = pheno; */
      /* *covOut = cov; */
      this->phenotype = pheno;
      this->covariate = cov;
    } else if (this->strategy == IMPUTE_HWE) {
      // impute missing genotypes
      imputeGenotypeByFrequency(&genotype, &this->random);
      /* *phenoOut = pheno; */
      /* *covOut = cov; */
      this->phenotype = pheno;
      this->covariate = cov;
    } else if (this->strategy == DROP) {
      // we process genotype matrix (people by marker)
      // if for the same people, any marker is empty, we will remove this people
      int idxToCopy = 0;
      for (int i = 0; i < (genotype).rows; ++i) {
        if (hasNoMissingGenotype(genotype, i)) {
          copyRow(genotype, i, &genotype, idxToCopy);
          copyRow(cov, i, &covariate, idxToCopy);
          copyRow(pheno, i, &phenotype, idxToCopy);
          rowLabel[idxToCopy] = originalRowLabel[i];
          idxToCopy++;
        }
      }
      genotype.Dimension(idxToCopy, genotype.cols);
      covariate.Dimension(idxToCopy, cov.cols);
      phenotype.Dimension(idxToCopy, pheno.cols);
    } else {
      logger->error("Uninitialized consolidation methods to handle missing data!");
      // return -1;
    };
  };
  bool hasNoMissingGenotype(Matrix& g, int r) {
    const int n = g.cols;
    for (int i = 0; i < n; ++i){
      if (g[r][i] < 0) return false;
    }
    return true;
  };
  void copyRow(Matrix& src, const int srcRow,
               Matrix* dest, const int destRow) {
    Matrix& m = *dest;
    if (m.cols < src.cols) {
      m.Dimension(m.rows, src.cols);
    }
    if (m.rows <= destRow) {
      m.Dimension(destRow + 1, m.cols);
    }
    const int n = m.cols;
    for (int i = 0; i < n; ++i){
      m[destRow][i] = src[srcRow][i];
    }
  };
  void copyColName(Matrix& src, Matrix* dest){
    dest->Dimension(dest->rows, src.cols);
    for (int i = 0; i < src.cols; ++i){
      dest->SetColumnLabel(i, src.GetColumnLabel(i));
    }
  };
  void setPhenotypeName(const std::vector<std::string>& name) {
    this->originalRowLabel = name;
    this->rowLabel = name;
  }
  const std::vector<std::string>& getRowLabel() const {
    return this->rowLabel;
  }
  Matrix& getGenotype(){
    return this->genotype;
  }
  Matrix& getPhenotype() {
    return this->phenotype;
  }
  Matrix& getCovariate() {
    return this->covariate;
  }
  Vector& getWeight() {
    return this->weight;
  }
  Result& getResult() {
    return this->result;
  }
  const int countGenotype(int columnIndex,
                          int* homRef,
                          int* het,
                          int* homAlt,
                          int* missing) const {
    if (columnIndex < 0 || columnIndex >= originalGenotype.cols) {
      return -1;
    }
    (*homRef) = (*het) = (*homAlt) = (*missing) = 0;
    for (int i = 0; i < originalGenotype.rows; ++i){
      int g = (int)originalGenotype[i][columnIndex];
      switch (g) {
        case 0:
          ++(*homRef);
          break;
        case 1:
          ++(*het);
          break;
        case 2:
          ++(*homAlt);
          break;
        default:
          if (originalGenotype[i][columnIndex] < 0) {
            ++(*missing);
          }
          break;
      }
    }
    return 0; //success
  }
private:
  int strategy;
  Random random;
  Matrix genotype;
  Matrix phenotype;
  Matrix covariate;
  Vector weight;
  Result result;
  Matrix originalGenotype;
  std::vector<std::string> originalRowLabel;
  std::vector<std::string> rowLabel;
}; // end DataConsolidator


#endif /* _DATACONSOLIDATOR_H_ */
