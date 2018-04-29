#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<math.h>
#include<time.h>

//#define SURFACE_AREA 1.1965e40 // 4*pi*(10 pc)**2 unit cm^2
//#define JANSKY(x) (3.34e4*(x)*(x))
#define M_AB(x) (-2.5*log10(x) + 8.9) // Convert Jansky to AB magnitude
#define TOL 1e-30 // Minimum Flux

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                             *
 * Profiling functions                                                         *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#ifdef TIMING
    #define MAX_BLOCK 100
    #define INTEGRATION 0
    #define DUST 1
    #define WORKING1 2
    #define WORKING2 3
    #define SUM 4
    #define FIT 5

    static double timer[MAX_BLOCK];
    static double counter[MAX_BLOCK];
    static char blockNames[MAX_BLOCK][128];
    static struct timespec g_sTime;
    static struct timespec g_eTime;
    static struct timespec g_sTime2;
    static struct timespec g_eTime2;


    void timing_start(char* text) {
        clock_gettime(CLOCK_REALTIME, &g_sTime);
        printf("#***********************************************************\n");
        printf("# %s\n", text);
    }


    void timing_end(void) {
        clock_gettime(CLOCK_REALTIME, &g_eTime);
        double elapsedTime = g_eTime.tv_sec - g_sTime.tv_sec \
                             + (g_eTime.tv_nsec - g_sTime.tv_nsec)/1e9;
        int minute = (int)elapsedTime/60;
        printf("# 100.0%% complete!\n");
        printf("# Done!\n");
        printf("# Elapsed time: %d min %.6f sec\n", minute, elapsedTime - minute*60);
        printf("#***********************************************************\n\n");
    }


    void init_profiler(void) {
        int iB;
        for(iB = 0; iB < MAX_BLOCK; ++iB) {
            timer[iB] = 0.;
            counter[iB] = 0;
        }
    }


    void profiler_start(char* name, int blockIdx) {
        clock_gettime(CLOCK_REALTIME, &g_sTime2);
        strcpy(blockNames[blockIdx], name);
    }


    void profiler_end(int blockIdx) {
        clock_gettime(CLOCK_REALTIME, &g_eTime2);
        timer[blockIdx] += g_eTime2.tv_sec - g_sTime2.tv_sec \
                           + (g_eTime2.tv_nsec - g_sTime2.tv_nsec)/1e9;
        counter[blockIdx] += 1;
    }


    void profiler_summary(void) {
        int iB, ncall;
        printf("#***********************************************************\n");
        for(iB = 0; iB < MAX_BLOCK; ++iB) {
            ncall = counter[iB];
            if (ncall == 0)
                continue;
            printf("# %s\n", blockNames[iB]);
            printf("#  call: %6d  total: %2.3f sec  mean: %2.3f ms\n", 
                   ncall, timer[iB], timer[iB]/ncall*1e3);
        }
        printf("#***********************************************************\n\n");
    }


    inline void report(int i, int tot) {
        int n = tot > 10 ? tot/10 : 1;
        if (i%n == 0) {
            printf("# %5.1f%% complete!\r", 100.*(i + 1)/tot);      
            fflush(stdout);
        }
    }
#endif


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                             *
 * Basic functions                                                             *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
FILE *open_file(char *fName, char *mode) {
/* Open the file with specific mode */
    FILE *fp;
    if ((fp = fopen(fName, mode)) == NULL) {
        printf("File open error: \"%s\"!\n", fName);
        exit(0);
    }
    printf("# File opened: \"%s\"!\n", fName);
    return fp;
}


double **malloc_2d_double(int nRow, int nCol) {
    int i;
    double **target = malloc(nRow*sizeof(double*));
    for(i = 0; i < nRow; ++i) 
        target[i] = (double*)malloc(nCol*sizeof(double));
    return target;
}


double **memcpy_2d_double(double **source, int nRow, int nCol) {
    int i;
    double **target = malloc_2d_double(nRow, nCol);
    for(i = 0; i < nRow; ++i) 
        memcpy(target[i], source[i], nCol*sizeof(double));
    return target;
}


void free_2d_double(double **p, int nRow) {
    int i;
    for(i = 0; i < nRow; ++i)
        free(p[i]);
    free(p);
}


inline int bisection_search(double a, double *x, int nX) {
    /* return idx such x[idx] <= a < x[idx + 1] 
     * a must be x[0] <= a < x[nX - 1]
     */
    unsigned int idx0 = 0;
    unsigned int idx1 = nX - 1;
    unsigned int idxMid;
    while(idx1 - idx0 > 1) {
        idxMid = (idx0 + idx1)/2;
        if(a >= x[idxMid])
            idx0 = idxMid;
        else if(a < x[idxMid])
            idx1 = idxMid;
    }
    return idx0;
}


inline double interp(double xp, double *x, double *y, int nPts) {
    /* Interpolate a given points */
    int idx0, idx1;
    if((xp < x[0]) || (xp > x[nPts - 1])) {
        printf("Error: Point %10.5e is beyond the interpolation region\n", xp);
        exit(0);
    }
    if (xp == x[nPts - 1])
        return y[nPts - 1];
    else {
        idx0 = bisection_search(xp, x, nPts);
        if (x[idx0] == xp)
            return y[idx0];
        idx1 = idx0 + 1;
        return y[idx0] + (y[idx1] - y[idx0])*(xp - x[idx0])/(x[idx1] - x[idx0]);
    }
}


inline double trapz_table(double *y, double *x, int nPts, double a, double b) {
    /* Integrate tabular data from a to b */
    int i;
    int idx0, idx1;
    double ya, yb;
    double I;
    if (x[0] > a) {
        printf("Error: Integration range %10.5e is beyond the tabular data\n", a);
        exit(0);
    }
    if (x[nPts - 1] < b) {
        printf("Error: Integration range %10.5e is beyond the tabular data\n", b); 
        exit(0);
    }
    if (a > b) {
        printf("Error: a must be smaller than b\n");
        exit(0);
    }
    idx0 = bisection_search(a, x, nPts);
    idx1 = idx0 + 1;

    ya = y[idx0] + (y[idx1] - y[idx0])*(a - x[idx0])/(x[idx1] - x[idx0]);
    if(b <= x[idx1]) {
        yb = y[idx0] + (y[idx1] - y[idx0])*(b - x[idx0])/(x[idx1] - x[idx0]);
        return (b - a)*(yb + ya)/2.;
    }
    else 
        I = (x[idx1] - a)*(y[idx1] + ya)/2.;

    for(i = idx1; i < nPts - 1; ++i) {
        if (x[i + 1] < b)
            I += (x[i + 1] - x[i])*(y[i + 1] + y[i])/2.;
        else if (x[i] < b) {
            yb = y[i] + (y[i + 1] - y[i])*(b - x[i])/(x[i + 1] - x[i]);
            I += (b - x[i])*(yb + y[i])/2.;
        }
        else
            break;
    }
    return I;
}


inline double trapz_filter(double *filter, double *flux, double *waves, int nWaves) {
    /* integrate the flux in a filter */
    int i;
    double y0 = filter[0]*flux[0];
    double y1;
    double I = 0.;
    for(i = 1; i < nWaves; ++i) {
        y1 = filter[i]*flux[i];
        I += (waves[i] - waves[i - 1])*(y0 + y1);
        y0 = y1;
    }
    return I/2.;
}


struct linResult {
    double slope;
    double intercept;
    double R;
};


inline struct linResult linregress(double *x, double *y, int nPts) {
    int i;
   
    double xSum = 0.;
    for(i = 0; i < nPts; ++i)
        xSum += x[i];

    double ySum = 0.;
    for(i = 0; i < nPts; ++i)
        ySum += y[i];

    double xxSum = 0.;
    for(i = 0; i < nPts; ++i)
        xxSum += x[i]*x[i];

    double xySum = 0.;
    for(i = 0; i < nPts; ++i)
        xySum += x[i]*y[i];

    double denominator = nPts*xxSum - xSum*xSum;
    
    double slope = (nPts*xySum - xSum*ySum)/denominator;
    double intercept = (xxSum*ySum - xSum*xySum)/denominator;

    double yReg;
    double delta;
    double ssRes = 0.;
    for(i = 0; i < nPts; ++i) {
        yReg = slope*x[i] + intercept;
        delta = yReg - y[i];
        ssRes += delta*delta;
    }

    double yMean = ySum/nPts;
    double ssTot = 0.;
    for(i = 0; i < nPts; ++i) {
        delta = yMean - y[i];
        ssTot += delta*delta;
    }

    double R = sqrt(1. - ssRes/ssTot);

    struct linResult result;
    result.slope = slope;
    result.intercept = intercept;
    result.R = R;
    return result;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                             *
 * Struct to store galaxy properites                                           *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
struct ssp {
    short index;
    float metals;
    float sfr;
};

struct csp {
    struct ssp *bursts;
    int nBurst;
};

struct gal_params {
    double z;
    int nAgeStep;
    double *ageStep;
    int nGal;
    int *indices;
    struct csp *histories;
};


void trim_gal_params(struct gal_params *galParams, int minZ, int maxZ) {
    /* Set the metallicity of each SSP to the given range */
    int iB, iG;

    float f_minZ = (minZ + 1.)/1000.;
    float f_maxZ = (maxZ + 1.)/1000.;
    int metals;
    int nGal = galParams->nGal;
    struct csp *pHistories = galParams->histories;
    int nBurst;
    struct ssp *pBursts;

    for(iG = 0; iG < nGal; ++iG) {
        nBurst = pHistories->nBurst;
        pBursts = pHistories->bursts;
        for(iB = 0; iB < nBurst; ++iB) {
            metals = (int)(pBursts->metals*1000 - .5);
            if (metals < minZ)
                pBursts->metals = f_minZ;
            else if (metals > maxZ)
                pBursts->metals = f_maxZ;
            ++pBursts;
        }
        ++pHistories;
    }
}


int *age_flag(struct csp *histories, int nAgeStep) {
    int iA, iB;

    int *ageFlag = malloc(nAgeStep*sizeof(int));
    int nB = histories->nBurst;
    struct ssp *bursts = histories->bursts;

    for(iA = 0; iA < nAgeStep; ++iA)
        ageFlag[iA] = 1;
    for(iB = 0; iB < nB; ++iB)
        ageFlag[bursts[iB].index] = 0;

    return ageFlag;
}


int *Z_flag(struct csp *histories, int nMaxZ) {
    int iZ, iB;

    int *ZFlag = malloc(nMaxZ*sizeof(int));
    int nB = histories->nBurst;
    struct ssp *bursts = histories->bursts;

    for(iZ = 0; iZ < nMaxZ; ++iZ)
        ZFlag[iZ] = 1;
    for(iB = 0; iB < nB; ++iB)
        ZFlag[(int)(1000*bursts[iB].metals - 0.5)] = 0;

    return ZFlag;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                             *
 * Functions to process SEDs                                                   *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// Struct for SED templates
struct sed_params {
    // Raw templates
    int minZ;
    int maxZ;
    int nZ;
    double *Z;
    int nWaves;
    double *waves;
    int nAge;
    double *age;
    double *raw;
    // Redshift
    double z;
    // Filters
    int nFlux;
    int nObs;
    int *nFilterWaves;
    double *filterWaves;
    double *filters;
    double *logWaves;
    // IGM absoprtion
    double *LyAbsorption;
    // Working templates
    int nAgeStep;
    double *ageStep;
    double *integrated;
    double *ready;
    double *working;
};


void shrink_templates_raw(struct sed_params *spectra, double maxAge) {
    if (spectra->filters == NULL)
        return;

    int iA, iF, iW, iZ;

    int nZ = spectra->nZ;
    int nWaves = spectra->nWaves;
    double *waves = spectra->waves;
    int nAge = spectra->nAge;
    double *age = spectra->age;
    double *raw = spectra->raw;
    double z = spectra->z;
    int nFlux = spectra->nFlux;
    int nRest = nFlux - spectra->nObs;
    int nFW;
    int *nFilterWaves = spectra->nFilterWaves;
    double *filterWaves = spectra->filterWaves;
    double *pFilterWaves;
    double *LyAbsorption = spectra->LyAbsorption;

    int inFlag = 0;
    int outFlag;
    int nNewWaves = 0;
    int *wavesIndices = malloc(nWaves*sizeof(int));
    double w;
    int nNewAge = 0;
    double *newWaves;
    double *newRaw;
    double *pNewRaw;
    double *newAbsorption;

    // Find shrinked wavelength ranges
    printf("#***********************************************************\n");
    printf("# Shrinked wavelength ranges:\n");
    for(iW = 1; iW < nWaves; ++iW) {
        w = waves[iW];
        pFilterWaves = filterWaves;
        outFlag = 1;
        for(iF = 0; iF < nFlux; ++iF) {
            nFW = nFilterWaves[iF];
            if (iF == nRest)
                w *= 1. + z;
            if (w > pFilterWaves[0] && w < pFilterWaves[nFW - 1]) {
                outFlag = 0;
                if (!inFlag) {
                    printf("#\t%.1f AA to ", waves[iW - 1]);
                    wavesIndices[nNewWaves++] = iW - 1;
                    wavesIndices[nNewWaves++] = iW;
                    inFlag = 1;
                }
                else {
                    wavesIndices[nNewWaves++] = iW;
                }
                break;
            }
            pFilterWaves += nFW;
        }
        if (inFlag && outFlag) {
            printf("%.1f AA\n", waves[iW]);
            wavesIndices[nNewWaves++] = iW;
            inFlag = 0;
        }
    }
    printf("# Original nWaves: %d\n", nWaves);
    printf("# Shrinked nWaves: %d\n", nNewWaves);
    // Find nNewAge
    printf("#\n# Shrinked age range:\n");
    for(iA = 0; age[iA] < maxAge; ++iA);
    nNewAge = iA + 1;
    spectra->nAge = nNewAge;
    printf("#\t%.2f Myr to %.2f Myr\n", age[0]*1e-6, age[iA]*1e-6);
    printf("# Original nAge: %d\n", nAge);
    printf("# Shrinked nAge: %d\n", nNewAge);
    printf("#***********************************************************\n\n");
    // Construct new wavelengths
    newWaves = (double*)malloc(nNewWaves*sizeof(double));
    for(iW = 0; iW < nNewWaves; ++iW) 
        newWaves[iW] = waves[wavesIndices[iW]];
    spectra->nWaves = nNewWaves;
    spectra->waves = newWaves;
    free(waves);
    // Construct new raw templates
    newRaw = (double*)malloc(nZ*nNewWaves*nNewAge*sizeof(double));
    pNewRaw = newRaw;
    for(iZ = 0; iZ < nZ; ++iZ)
        for(iW = 0; iW < nNewWaves; ++iW)
            for(iA = 0; iA < nNewAge; ++iA)
                *pNewRaw++ = raw[(iZ*nWaves + wavesIndices[iW])*nAge + iA];
    spectra->raw = newRaw;
    free(raw);
    // Construct new IGM absorption
    if (LyAbsorption != NULL) {
        newAbsorption = (double*)malloc(nNewWaves*sizeof(double));
        for(iW = 0; iW < nNewWaves; ++iW)
            newAbsorption[iW] = LyAbsorption[wavesIndices[iW]];
        spectra->LyAbsorption = newAbsorption;
        free(LyAbsorption);
    }
    free(wavesIndices);
}


void init_templates_integrated(struct sed_params *spectra) {
    #ifdef TIMING
        profiler_start("Integration over time", INTEGRATION);
    #endif
    int iA, iW, iZ;
    double *pData;

    int nAgeStep = spectra->nAgeStep;
    double *ageStep = spectra->ageStep;
    int nAge = spectra->nAge;
    double *age = spectra->age;
    int nWaves = spectra->nWaves; 
    int nZ = spectra->nZ;
    double *data = spectra->raw;
    // Spectra after integration over time
    // The first dimension refers to metallicites and ages
    // The last dimension refers to wavelengths
    double *intData = malloc(nZ*nAgeStep*nWaves*sizeof(double));

    for(iZ = 0; iZ < nZ; ++iZ) 
        for(iA = 0; iA < nAgeStep; ++iA) {
            pData = intData + (iZ*nAgeStep + iA)*nWaves;
            for(iW = 0; iW < nWaves; ++iW) {
                if (iA == 0) 
                    // The first time step of SED templates is typicall not zero
                    // Here assumes that the templates is zero beween zero
                    // and the first time step
                    pData[iW] = trapz_table(data + (iZ*nWaves + iW)*nAge, age, nAge, 
                                            age[0], ageStep[iA]);
                else
                    pData[iW] = trapz_table(data + (iZ*nWaves + iW)*nAge, age, nAge, 
                                            ageStep[iA - 1], ageStep[iA]);
            }
        }
    spectra->integrated = intData;
    #ifdef TIMING
        profiler_end(INTEGRATION);
    #endif
}
 

struct dust_params {
    double tauUV_ISM;
    double nISM;
    double tauUV_BC;
    double nBC;
    double tBC;
};


inline void dust_absorption(struct sed_params *spectra, struct dust_params *dustParams,
                            int *ageFlag) {
    /* tBC: life time of the birth clound
     * nu: fraction of ISM dust absorption
     * tauUV: V-band absorption optical depth
     * nBC: power law index of tauBC
     * nISM: power law index of tauISM
     * 
     * Reference: da Cunha et al. 2008
     */
    int iA, iW, i, n;
    double *pData; 

    int nAge = spectra->nAge;
    double *age = spectra->age;
    double *rawData = spectra->raw;
    int nWaves = spectra->nWaves;
    double *waves = spectra->waves;
    int nZ = spectra->nZ;

    int nAgeStep = spectra->nAgeStep;
    double *ageStep = spectra->ageStep;
    double *data = spectra->ready;

    int iAgeBC;
    double t0, t1;
    double tauUV_ISM = dustParams->tauUV_ISM;
    double nISM = dustParams->nISM;
    double tauUV_BC = dustParams->tauUV_BC;
    double nBC = dustParams->nBC;
    double tBC = dustParams->tBC;
    double *transISM = malloc(nWaves*sizeof(double));
    double *transBC = malloc(nWaves*sizeof(double));
    double ratio;

    // Find the time inverval containning the birth cloud
    if (tBC >= ageStep[nAgeStep - 1]) {
        iAgeBC = nAgeStep;
        t0 = 0.;
        t1 = 0.;
    }
    else if (tBC < ageStep[0]) {
        iAgeBC = 0;
        t0 = age[0];
        t1 = ageStep[0];
        if (tBC < t0)
            tBC = t0;
    }
    else {
        iAgeBC = bisection_search(tBC, ageStep, nAgeStep) + 1;
        t0 = ageStep[iAgeBC - 1];
        t1 = ageStep[iAgeBC];
    } 
    
    // Compute the optical depth of both the birth cloud and the ISM
    for(iW = 0; iW < nWaves; ++iW) {
        ratio = waves[iW]/1600.;
        transISM[iW] = exp(-tauUV_ISM*pow(ratio, nISM));
        transBC[iW] = exp(-tauUV_BC*pow(ratio, nBC));
    }
    
    // t_s < tBC < t_s + dt
    if (iAgeBC != nAgeStep && !ageFlag[iAgeBC]) {
        n = nZ*nWaves;
        for(i = 0; i < n; ++i) {
            iW = i%nWaves;
            pData = data + (i/nWaves*nAgeStep + iAgeBC)*nWaves;
            pData[iW] = transBC[iW] \
                *trapz_table(rawData + i*nAge, age, nAge, t0, tBC) \
                + trapz_table(rawData + i*nAge, age, nAge, tBC, t1);
        }     
    }
    
    // tBC > t_s       
    n = iAgeBC*nZ;
    for(i = 0; i < n; ++i) {
        iA = i%iAgeBC;
        if (ageFlag[iA])
            continue;
        pData = data + (i/iAgeBC*nAgeStep + iA)*nWaves;
        for(iW = 0; iW < nWaves; ++iW) 
            pData[iW] *= transBC[iW];
    }
    
    n = nAgeStep*nZ;
    for(i = 0; i < n; ++i) {
        if (ageFlag[i%nAgeStep])
            continue;
        pData = data + i*nWaves;
        for(iW = 0; iW < nWaves; ++iW) 
            pData[iW] *= transISM[iW];
    }

    free(transISM);
    free(transBC);
}


inline void init_templates_working(struct sed_params *spectra, struct csp *pHistories,
                                   struct dust_params *dustParams, int iG) {
    int *ageFlag = NULL;
    int *ZFlag = NULL;

    int minZ = spectra->minZ;
    int maxZ = spectra->maxZ;   
    int nMaxZ = maxZ - minZ + 1;
    int nZ = spectra->nZ;
    int nWaves = spectra->nWaves;
    int nAgeStep = spectra->nAgeStep;
    size_t readySize = nZ*nAgeStep*nWaves*sizeof(double);
    double *readyData = spectra->ready;

    if (dustParams != NULL) {
        memcpy(readyData, spectra->integrated, readySize);
        ageFlag = age_flag(pHistories, nAgeStep);
        ZFlag = Z_flag(pHistories, nMaxZ);
        dust_absorption(spectra, dustParams + iG, ageFlag);
    }
    else if (iG == -1) {
        memcpy(readyData, spectra->integrated, readySize);
        ageFlag = calloc(nAgeStep, sizeof(int));
        ZFlag = calloc(nMaxZ, sizeof(int));
    }
    else
        return;

    int iA, iW, iF, iFW, iZ, i, n;

    double *Z = spectra->Z;
    double *waves = spectra->waves;
    double *pWaves;

    double z = spectra->z;
    int nFlux = spectra->nFlux;
    int nObs = spectra->nObs;
    int nRest = nFlux - nObs;
    int nFW;
    int *nFilterWaves = spectra->nFilterWaves;
    double *filterWaves = spectra->filterWaves;
    double *filters = spectra->filters;
    double *pFilterWaves = filterWaves;
    double *pFilters = filters;
    double *filterData;
    double I;
    double *LyAbsorption = spectra->LyAbsorption;

    double *workingData = spectra->working;
    double *obsWaves = NULL;
    double *obsData = NULL;
    if (nObs > 0) {
        obsWaves = (double*)malloc(nWaves*sizeof(double));
        obsData = (double*)malloc(readySize);
    }
    double *pData;
    double *pObsData;
    double *pReadyData;
    // Spectra to be interploated along metallicities
    // The first dimension refers to filters/wavelengths and ages
    // Thw last dimension refers to metallicites
    double *refSpectra = malloc(nFlux*nAgeStep*nZ*sizeof(double));
    double interpZ;
    double *pRefData;

    if (nObs > 0) {
        // Transform everything to observer frame
        // Note the fluxes in this case is a function of wavelength
        // Therefore the fluxes has a factor of 1/(1 + z)
        for(iW = 0; iW < nWaves; ++iW)
            obsWaves[iW] = waves[iW]*(1. + z);
        n = nZ*nAgeStep;
        for(i = 0; i < n; ++i) {
            pData = readyData + i*nWaves;
            pObsData = obsData + i*nWaves;
            for(iW = 0; iW < nWaves; ++iW)
                pObsData[iW] = pData[iW]/(1. + z);           
        }
        if (LyAbsorption != NULL)
            // Add IGM absorption
            for(i = 0; i < n; ++i) {
                pObsData = obsData + i*nWaves;
                for(iW = 0; iW < nWaves; ++iW)
                    pObsData[iW] *= LyAbsorption[iW];
                }       
    }
    if (filters == NULL) {
        // Tranpose the templates such that the last dimension is the metallicity
        if (nObs > 0) 
            pReadyData = obsData;
        else
            pReadyData = readyData;
        n = nZ*nAgeStep;
        for(i = 0; i < n; ++i) {
            pData = pReadyData + i*nWaves;
            for(iW = 0; iW < nWaves; ++iW)
                refSpectra[(iW*nAgeStep + i%nAgeStep)*nZ + i/nAgeStep] = pData[iW];
        }
    }
    else {
        // Intgrate SED templates over filters
        pWaves = waves;
        pReadyData = readyData;
        pRefData = refSpectra;
        for(iF = 0; iF < nFlux; ++iF) {
            nFW = nFilterWaves[iF];
            filterData = (double*)malloc(nFW*sizeof(double));
            if (iF == nRest) {
                pWaves = obsWaves;
                pReadyData = obsData;
            }
            n = nAgeStep*nZ;
            for(i = 0; i < n; ++i) {
                iA = i/nZ;
                if (ageFlag[iA])
                    continue;
                pData = pReadyData + (i%nZ*nAgeStep + iA)*nWaves;
                for(iFW= 0; iFW < nFW; ++iFW)
                    filterData[iFW] = interp(pFilterWaves[iFW], 
                                             pWaves, pData, nWaves);
                for(iFW = 0; iFW < nFW; ++iFW)
                    filterData[iFW] *= pFilters[iFW];
                I = 0.;
                for(iFW = 1; iFW < nFW; ++iFW)
                    I += (pFilterWaves[iFW] - pFilterWaves[iFW - 1]) \
                         *(filterData[iFW] + filterData[iFW - 1]);
                pRefData[iF*n + i] = I/2.;
            }
            free(filterData);
            pFilterWaves += nFW;
            pFilters += nFW;
        }
    }
    // Interploate SED templates along metallicities
    n = nMaxZ*nAgeStep;
    for(i = 0; i < n; ++i) {
        iZ = i/nAgeStep;
        if (ZFlag[iZ])
            continue;
        iA = i%nAgeStep;
        if (ageFlag[iA])
            continue;
        interpZ = (minZ + iZ + 1.)/1000.;
        pData = workingData + i*nFlux;
        for(iF = 0; iF < nFlux; ++iF) 
            pData[iF] = interp(interpZ, Z, refSpectra + (iF*nAgeStep+ iA)*nZ, nZ);
    }
 
    free(ageFlag);
    free(ZFlag);
    free(obsWaves);
    free(obsData);
    free(refSpectra);

}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                             *
 * Primary Functions                                                           *
 *                                                                             *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
double *composite_spectra_cext(struct sed_params *spectra,
                               struct gal_params *galParams, struct dust_params *dustParams,
                               short outType, short nThread) {
    #ifdef TIMING
        init_profiler();
        timing_start("Compute magnitudes");
    #endif

    int iF, iG, iFG;

    int minZ = spectra->minZ;
    int maxZ = spectra->maxZ;
    int nMaxZ = maxZ - minZ + 1;
    // Initialise galaxies parameters
    trim_gal_params(galParams, minZ, maxZ);
    int nAgeStep= galParams->nAgeStep;
    double *ageStep = galParams->ageStep;
    int nGal = galParams->nGal;
    struct csp *histories = galParams->histories;
    // Generate templates
    int nFlux = spectra->nFlux;
    size_t readySize = spectra->nZ*nAgeStep*spectra->nWaves*sizeof(double);
    size_t workingSize = nMaxZ*nAgeStep*nFlux*sizeof(double);
    spectra->nAgeStep = nAgeStep;
    spectra->ageStep = ageStep;
    init_templates_integrated(spectra);
    spectra->ready = NULL;
    spectra->working = NULL;
    // Initialise outputs
    double *output = malloc(nGal*nFlux*sizeof(double));
    double *pOutput = output;
    for(iFG = 0; iFG < nGal*nFlux; ++iFG)
        *pOutput++ = TOL;

    #ifdef TIMING
        profiler_start("Summation over progenitors", SUM);
    #endif
    #pragma omp parallel \
    default(none) \
    firstprivate(spectra, dustParams, \
                 nAgeStep, ageStep, nGal, histories, \
                 nFlux, readySize, workingSize, \
                 output) \
    num_threads(nThread)
    {
        int iF, iG;
        double *pData;
        double *pOutput;

        int iP, nProg;
        struct csp *pHistories;
        struct ssp *pBursts;
        double sfr;
        int metals;

        struct sed_params omp_spectra;
        memcpy(&omp_spectra, spectra, sizeof(struct sed_params));
        double *readyData = malloc(readySize);
        double *workingData = malloc(workingSize);
        omp_spectra.ready = readyData;
        omp_spectra.working = workingData;
        if (dustParams == NULL)
            init_templates_working(&omp_spectra, histories, NULL, -1);

        #pragma omp for schedule(static, 1)
        for(iG = 0; iG < nGal; ++iG) {
            pHistories = histories + iG;
            // Add dust absorption to SED templates
            init_templates_working(&omp_spectra, pHistories, dustParams, iG);
            // Sum contributions from all progenitors
            nProg = pHistories->nBurst;
            pOutput = output + iG*nFlux;
            for(iP = 0; iP < nProg; ++iP) {
                pBursts = pHistories->bursts + iP;
                sfr = pBursts->sfr;
                metals = (int)(pBursts->metals*1000 - .5);
                pData = workingData + (metals*nAgeStep + pBursts->index)*nFlux;
                for(iF = 0 ; iF < nFlux; ++iF)
                    pOutput[iF] += sfr*pData[iF];
            }
            #ifdef TIMING
                report(iG, nGal);
            #endif
        }
        free(readyData);
        free(workingData);
    }
    free(spectra->integrated);
    #ifdef TIMING
        profiler_end(SUM);
    #endif

    if (outType == 0) {
        pOutput = output;
        for(iFG = 0; iFG < nFlux*nGal; ++iFG) {
            *pOutput = M_AB(*pOutput);
            ++pOutput;
        }
        #ifdef TIMING
            timing_end();
            profiler_summary();
        #endif
        return output;
    }
    else if (outType == 1) {
        #ifdef TIMING
            timing_end();
            profiler_summary();
        #endif
        return output;
    }
    
    // Fit UV slopes
    #ifdef TIMING
        profiler_start("Slope fit", FIT);
    #endif
    int nR = 3;
    struct linResult result;
    int nFit = nFlux - 1;
    double *logf = malloc(nFit*sizeof(double));
    double *logWaves = spectra->logWaves;

    output = (double*)realloc(output, (nFlux + nR)*nGal*sizeof(double));
    pOutput = output + nFlux*nGal;
    double *pFit = output;

    for(iG = 0; iG < nGal; ++iG) {
        for(iF = 0; iF < nFit; ++iF) 
            logf[iF] = log(pFit[iF]);
        pFit += nFlux;
        //printf("waves = %.1f, logf = %.1f\n", logWaves[1], logf[1]);
        result = linregress(logWaves, logf, nFit);
        pOutput[0] = (double)result.slope;
        pOutput[1] = (double)result.intercept;
        pOutput[2] = (double)result.R;
        pOutput += nR;
        //printf("Slope = %.1f\n", result.slope);
    }
    // Convert to AB magnitude
    pOutput = output + nFit;
    for(iG = 0; iG < nGal; ++iG) {
        *pOutput = M_AB(*pOutput);
        pOutput += nFlux;
    }
    #ifdef TIMING
        profiler_end(FIT);
        timing_end();
        profiler_summary();
    #endif
    return output;
}

