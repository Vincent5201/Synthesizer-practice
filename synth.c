#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SAMPLE_RATE 44100
#define SYNTH_NODES 4
#define SYNTH_VOICES 2
#define SYNTH_MS(ms) ((uint32_t)(ms * SAMPLE_RATE) / 1000)      // change ms to samples
#define SYNTH_HZ_TO_PHASE(frequency) \
        (q31_t)((frequency * Q31_MAX) / SAMPLE_RATE)            // change samples to phases
typedef int32_t q31_t;
#define Q31_MAX 0x7FFFFFFF
#define Q31_MIN 0x80000000
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// basic components of Synthesizer
typedef struct {
    q31_t *phase_incr;      // frequency
    q31_t *detune;          // fintune / lfo / vibration
    q31_t (*wavegen)(q31_t input, q31_t dt); 
} synth_oscillator_t;

typedef struct {
    q31_t attack, decay, sustain, release;
} synth_envelope_t;

typedef struct {
    q31_t *input;
    int64_t low, band;      // for SVF
    q31_t factor;           // cutoff frequency
    q31_t res;              // resonance
} synth_filter_t;

typedef struct {
    q31_t *inputs[3];
} synth_mixer_t;

typedef enum {
    SYNTH_NODE_NONE = 0,
    SYNTH_NODE_OSCILLATOR,
    SYNTH_NODE_ENVELOPE,
    SYNTH_NODE_FILTER_LP,
    SYNTH_NODE_FILTER_HP,
    SYNTH_NODE_MIXER,
    SYNTH_NODE_END
} synth_node_type_t;

typedef struct {
    int32_t state;
    q31_t *gain;
    q31_t output;
    synth_node_type_t type;
    union {
        synth_oscillator_t osc;
        synth_envelope_t env;
        synth_filter_t filter;
        synth_mixer_t mixer;
    };
} synth_node_t;         // structure for a node

typedef struct {
    uint8_t note;
    uint8_t gate : 1;
    q31_t phase_incr;
    synth_node_t nodes[SYNTH_NODES];
} synth_voice_t;            // structure for a voice

synth_voice_t synth_voices[SYNTH_VOICES];

// smooth discontinuity points of the sawtooth/square wave
static q31_t poly_blep(q31_t phase, q31_t dt) {
    if (dt == 0)
        return 0;

    if (phase < dt) {
        int64_t t = ((int64_t)phase << 31) / dt;
        q31_t t_q31 = (q31_t)t;
        q31_t t_sq = (q31_t)(((int64_t)t_q31 * t_q31) >> 31);
        return (q31_t)((int64_t)2 * t_q31 - t_sq - Q31_MAX);        // 2t - t^2 - 1

    } else if (phase > (Q31_MAX - dt)) {
        int64_t t = ((int64_t)(phase - Q31_MAX) << 31) / dt;
        q31_t t_q31 = (q31_t)t;
        q31_t t_sq = (q31_t)(((int64_t)t_q31 * t_q31) >> 31);
        return (q31_t)(t_sq + (int64_t)2 * t_q31 + Q31_MAX);        // 2t + t^2 - 1
    }
    return 0;
}

// waves
q31_t sawtooth_wave(q31_t input, q31_t dt) {
    q31_t naive = (q31_t)(((int64_t)input * 2) - Q31_MAX);
    return naive - poly_blep(input, dt);
}

q31_t square_wave(q31_t input, q31_t dt) {
    q31_t naive = (input < Q31_MAX / 2) ? -Q31_MAX : Q31_MAX;
    naive += poly_blep(input, dt);
    q31_t phase2 = (input + 0x40000000) & 0x7FFFFFFF;
    naive -= poly_blep(phase2, dt);
    return naive;
}

#define SIN_LUT_BITS 10
#define SIN_LUT_SIZE (1 << SIN_LUT_BITS)

static const q31_t sin_lut[SIN_LUT_SIZE + 1] = {
    0, 3294197, 6588386, 9882561, 13176711, 16470831, 19764912, 23058947,
    26352927, 29646846, 32940694, 36234465, 39528151, 42821744, 46115236,
    49408619, 52701886, 55995030, 59288041, 62580913, 65873638, 69166207,
    72458614, 75750851, 79042909, 82334781, 85626459, 88917936, 92209204,
    95500255, 98791081, 102081674, 105372028, 108662133, 111951983, 115241569,
    118530884, 121819920, 125108670, 128397125, 131685278, 134973121, 138260647,
    141547847, 144834714, 148121240, 151407418, 154693239, 157978697, 161263783,
    164548489, 167832808, 171116732, 174400253, 177683365, 180966058, 184248325,
    187530158, 190811551, 194092494, 197372981, 200653003, 203932553, 207211623,
    210490205, 213768293, 217045877, 220322950, 223599506, 226875534, 230151030,
    233425983, 236700387, 239974234, 243247517, 246520227, 249792357, 253063900,
    256334846, 259605190, 262874923, 266144037, 269412525, 272680379, 275947591,
    279214154, 282480060, 285745301, 289009870, 292273759, 295536960, 298799466,
    302061268, 305322360, 308582733, 311842381, 315101294, 318359466, 321616889,
    324873555, 328129456, 331384586, 334638935, 337892497, 341145265, 344397229,
    347648383, 350898719, 354148229, 357396906, 360644742, 363891729, 367137860,
    370383127, 373627522, 376871039, 380113668, 383355403, 386596236, 389836160,
    393075166, 396313247, 399550395, 402786603, 406021864, 409256169, 412489511,
    415721883, 418953276, 422183683, 425413097, 428641510, 431868914, 435095302,
    438320666, 441544999, 444768293, 447990540, 451211733, 454431865, 457650927,
    460868912, 464085812, 467301621, 470516330, 473729932, 476942419, 480153783,
    483364018, 486573116, 489781068, 492987869, 496193509, 499397981, 502601278,
    505803393, 509004317, 512204044, 515402565, 518599874, 521795962, 524990823,
    528184448, 531376830, 534567962, 537757836, 540946445, 544133780, 547319836,
    550504603, 553688075, 556870244, 560051103, 563230644, 566408859, 569585742,
    572761285, 575935479, 579108319, 582279796, 585449902, 588618631, 591785975,
    594951926, 598116478, 601279622, 604441351, 607601658, 610760535, 613917975,
    617073970, 620228513, 623381597, 626533214, 629683356, 632832017, 635979189,
    639124864, 642269036, 645411696, 648552837, 651692452, 654830534, 657967075,
    661102067, 664235504, 667367378, 670497682, 673626407, 676753548, 679879096,
    683003045, 686125386, 689246113, 692365217, 695482693, 698598532, 701712727,
    704825271, 707936157, 711045377, 714152923, 717258789, 720362967, 723465451,
    726566231, 729665302, 732762657, 735858286, 738952185, 742044344, 745134758,
    748223418, 751310317, 754395449, 757478805, 760560379, 763640163, 766718150,
    769794333, 772868705, 775941258, 779011985, 782080880, 785147933, 788213140,
    791276491, 794337981, 797397601, 800455345, 803511206, 806565176, 809617248,
    812667415, 815715669, 818762004, 821806412, 824848887, 827889421, 830928006,
    833964637, 836999305, 840032003, 843062725, 846091463, 849118210, 852142958,
    855165702, 858186434, 861205146, 864221831, 867236483, 870249094, 873259658,
    876268167, 879274613, 882278991, 885281292, 888281511, 891279639, 894275670,
    897269597, 900261412, 903251109, 906238680, 909224119, 912207418, 915188571,
    918167571, 921144410, 924119081, 927091578, 930061893, 933030020, 935995951,
    938959680, 941921199, 944880502, 947837581, 950792430, 953745042, 956695410,
    959643526, 962589384, 965532977, 968474299, 971413341, 974350097, 977284561,
    980216725, 983146582, 986074126, 988999350, 991922247, 994842809, 997761030,
    1000676904, 1003590423, 1006501580, 1009410369, 1012316783, 1015220815,
    1018122458, 1021021705, 1023918549, 1026812984, 1029705003, 1032594599,
    1035481765, 1038366494, 1041248781, 1044128616, 1047005996, 1049880911,
    1052753356, 1055623323, 1058490807, 1061355800, 1064218295, 1067078287,
    1069935767, 1072790729, 1075643168, 1078493075, 1081340444, 1084185269,
    1087027543, 1089867258, 1092704410, 1095538990, 1098370992, 1101200409,
    1104027236, 1106851464, 1109673088, 1112492100, 1115308495, 1118122266,
    1120933405, 1123741907, 1126547764, 1129350971, 1132151520, 1134949405,
    1137744620, 1140537157, 1143327010, 1146114173, 1148898639, 1151680402,
    1154459455, 1157235791, 1160009404, 1162780287, 1165548434, 1168313839,
    1171076494, 1173836394, 1176593532, 1179347901, 1182099495, 1184848307,
    1187594331, 1190337561, 1193077990, 1195815611, 1198550418, 1201282406,
    1204011566, 1206737893, 1209461381, 1212182022, 1214899812, 1217614742,
    1220326808, 1223036001, 1225742317, 1228445749, 1231146290, 1233843934,
    1236538674, 1239230505, 1241919420, 1244605413, 1247288476, 1249968605,
    1252645793, 1255320033, 1257991319, 1260659645, 1263325004, 1265987391,
    1268646799, 1271303221, 1273956652, 1276607085, 1279254514, 1281898933,
    1284540336, 1287178716, 1289814067, 1292446383, 1295075658, 1297701885,
    1300325059, 1302945173, 1305562221, 1308176197, 1310787094, 1313394908,
    1315999630, 1318601256, 1321199779, 1323795194, 1326387493, 1328976671,
    1331562722, 1334145640, 1336725418, 1339302051, 1341875532, 1344445856,
    1347013016, 1349577006, 1352137821, 1354695454, 1357249900, 1359801151,
    1362349203, 1364894049, 1367435684, 1369974100, 1372509293, 1375041257,
    1377569984, 1380095471, 1382617709, 1385136695, 1387652420, 1390164881,
    1392674071, 1395179983, 1397682612, 1400181953, 1402677998, 1405170743,
    1407660182, 1410146308, 1412629116, 1415108600, 1417584754, 1420057573,
    1422527049, 1424993179, 1427455955, 1429915373, 1432371425, 1434824107,
    1437273413, 1439719337, 1442161873, 1444601016, 1447036759, 1449469097,
    1451898024, 1454323535, 1456745624, 1459164285, 1461579512, 1463991301,
    1466399644, 1468804536, 1471205973, 1473603947, 1475998454, 1478389488,
    1480777043, 1483161114, 1485541694, 1487918779, 1490292363, 1492662440,
    1495029005, 1497392051, 1499751575, 1502107569, 1504460028, 1506808947,
    1509154321, 1511496144, 1513834409, 1516169113, 1518500249, 1520827811,
    1523151796, 1525472195, 1527789006, 1530102221, 1532411836, 1534717845,
    1537020242, 1539319023, 1541614182, 1543905713, 1546193611, 1548477871,
    1550758487, 1553035454, 1555308767, 1557578419, 1559844407, 1562106724,
    1564365365, 1566620326, 1568871600, 1571119182, 1573363067, 1575603250,
    1577839725, 1580072488, 1582301532, 1584526853, 1586748446, 1588966305,
    1591180424, 1593390800, 1595597426, 1597800298, 1599999410, 1602194757,
    1604386334, 1606574135, 1608758156, 1610938392, 1613114836, 1615287485,
    1617456334, 1619621376, 1621782607, 1623940021, 1626093615, 1628243382,
    1630389317, 1632531417, 1634669674, 1636804086, 1638934645, 1641061348,
    1643184190, 1645303165, 1647418268, 1649529495, 1651636840, 1653740299,
    1655839866, 1657935537, 1660027307, 1662115171, 1664199123, 1666279160,
    1668355275, 1670427465, 1672495724, 1674560047, 1676620430, 1678676868,
    1680729356, 1682777889, 1684822462, 1686863071, 1688899710, 1690932375,
    1692961061, 1694985763, 1697006477, 1699023198, 1701035921, 1703044641,
    1705049354, 1707050054, 1709046738, 1711039400, 1713028036, 1715012641,
    1716993210, 1718969739, 1720942223, 1722910658, 1724875039, 1726835360,
    1728791618, 1730743809, 1732691926, 1734635967, 1736575926, 1738511798,
    1740443579, 1742371265, 1744294851, 1746214333, 1748129706, 1750040965,
    1751948106, 1753851124, 1755750016, 1757644776, 1759535400, 1761421884,
    1763304223, 1765182413, 1767056449, 1768926327, 1770792043, 1772653592,
    1774510969, 1776364171, 1778213193, 1780058031, 1781898680, 1783735136,
    1785567395, 1787395452, 1789219303, 1791038944, 1792854371, 1794665579,
    1796472564, 1798275321, 1800073847, 1801868137, 1803658188, 1805443994,
    1807225552, 1809002857, 1810775905, 1812544693, 1814309215, 1816069468,
    1817825448, 1819577150, 1821324571, 1823067705, 1824806550, 1826541101,
    1828271354, 1829997305, 1831718950, 1833436285, 1835149305, 1836858007,
    1838562387, 1840262440, 1841958163, 1843649552, 1845336602, 1847019310,
    1848697672, 1850371684, 1852041342, 1853706642, 1855367579, 1857024151,
    1858676353, 1860324182, 1861967633, 1863606703, 1865241387, 1866871682,
    1868497584, 1870119090, 1871736195, 1873348896, 1874957188, 1876561068,
    1878160533, 1879755578, 1881346200, 1882932395, 1884514160, 1886091490,
    1887664381, 1889232831, 1890796835, 1892356390, 1893911493, 1895462138,
    1897008324, 1898550045, 1900087299, 1901620082, 1903148391, 1904672221,
    1906191569, 1907706431, 1909216805, 1910722686, 1912224071, 1913720956,
    1915213339, 1916701214, 1918184579, 1919663431, 1921137766, 1922607579,
    1924072869, 1925533631, 1926989863, 1928441559, 1929888719, 1931331336,
    1932769410, 1934202935, 1935631909, 1937056328, 1938476189, 1939891489,
    1941302224, 1942708390, 1944109986, 1945507007, 1946899449, 1948287311,
    1949670588, 1951049277, 1952423376, 1953792880, 1955157786, 1956518092,
    1957873794, 1959224889, 1960571374, 1961913245, 1963250500, 1964583135,
    1965911147, 1967234533, 1968553290, 1969867415, 1971176905, 1972481756,
    1973781966, 1975077531, 1976368449, 1977654716, 1978936329, 1980213286,
    1981485584, 1982753218, 1984016187, 1985274488, 1986528117, 1987777071,
    1989021348, 1990260945, 1991495858, 1992726085, 1993951623, 1995172470,
    1996388621, 1997600075, 1998806828, 2000008877, 2001206221, 2002398855,
    2003586778, 2004769986, 2005948476, 2007122247, 2008291294, 2009455616,
    2010615209, 2011770071, 2012920199, 2014065591, 2015206243, 2016342154,
    2017473319, 2018599738, 2019721406, 2020838322, 2021950482, 2023057885,
    2024160527, 2025258407, 2026351520, 2027439866, 2028523440, 2029602242,
    2030676267, 2031745514, 2032809981, 2033869663, 2034924560, 2035974669,
    2037019987, 2038060511, 2039096240, 2040127170, 2041153300, 2042174627,
    2043191148, 2044202862, 2045209765, 2046211856, 2047209132, 2048201590,
    2049189229, 2050172046, 2051150039, 2052123205, 2053091543, 2054055049,
    2055013722, 2055967559, 2056916558, 2057860717, 2058800034, 2059734506,
    2060664132, 2061588909, 2062508834, 2063423906, 2064334123, 2065239482,
    2066139982, 2067035619, 2067926393, 2068812301, 2069693340, 2070569510,
    2071440807, 2072307229, 2073168776, 2074025444, 2074877232, 2075724137,
    2076566158, 2077403293, 2078235539, 2079062895, 2079885359, 2080702928,
    2081515602, 2082323377, 2083126253, 2083924227, 2084717297, 2085505461,
    2086288718, 2087067066, 2087840503, 2088609027, 2089372636, 2090131329,
    2090885104, 2091633958, 2092377891, 2093116900, 2093850983, 2094580140,
    2095304368, 2096023666, 2096738031, 2097447462, 2098151959, 2098851517,
    2099546137, 2100235817, 2100920555, 2101600349, 2102275197, 2102945099,
    2103610052, 2104270056, 2104925108, 2105575206, 2106220350, 2106860538,
    2107495769, 2108126040, 2108751350, 2109371699, 2109987084, 2110597504,
    2111202957, 2111803443, 2112398959, 2112989505, 2113575078, 2114155678,
    2114731304, 2115301953, 2115867624, 2116428317, 2116984030, 2117534761,
    2118080509, 2118621274, 2119157053, 2119687845, 2120213650, 2120734465,
    2121250290, 2121761124, 2122266965, 2122767812, 2123263664, 2123754520,
    2124240379, 2124721239, 2125197099, 2125667958, 2126133816, 2126594670,
    2127050521, 2127501366, 2127947205, 2128388037, 2128823860, 2129254674,
    2129680478, 2130101271, 2130517051, 2130927817, 2131333570, 2131734307,
    2132130028, 2132520732, 2132906418, 2133287086, 2133662733, 2134033359,
    2134398964, 2134759547, 2135115106, 2135465641, 2135811151, 2136151635,
    2136487093, 2136817524, 2137142926, 2137463299, 2137778643, 2138088956,
    2138394238, 2138694489, 2138989706, 2139279891, 2139565041, 2139845157,
    2140120238, 2140390283, 2140655291, 2140915262, 2141170196, 2141420091,
    2141664947, 2141904763, 2142139539, 2142369275, 2142593969, 2142813622,
    2143028233, 2143237800, 2143442325, 2143641805, 2143836242, 2144025634,
    2144209981, 2144389282, 2144563537, 2144732746, 2144896908, 2145056023,
    2145210091, 2145359111, 2145503082, 2145642005, 2145775879, 2145904703,
    2146028478, 2146147204, 2146260879, 2146369504, 2146473078, 2146571602,
    2146665074, 2146753495, 2146836865, 2146915182, 2146988448, 2147056662,
    2147119824, 2147177933, 2147230990, 2147278994, 2147321945, 2147359843,
    2147392689, 2147420481, 2147443221, 2147460907, 2147473540, 2147481120, 2147483647};

// only need to store 1/4 of sine wave
static inline q31_t q31_sin(q31_t phase) {
    uint32_t p = (uint32_t)phase;
    uint32_t quad = p >> 30;     // 0..3
    uint32_t frac = (p >> (30 - SIN_LUT_BITS)) & (SIN_LUT_SIZE - 1);
    uint32_t frac_next = frac + 1;

    q31_t a = sin_lut[frac];
    q31_t b = sin_lut[frac_next];

    uint32_t interp = (p >> (30 - SIN_LUT_BITS - 8)) & 0xFF;
    q31_t val = a + (q31_t)(((int64_t)(b - a) * interp) >> 8);

    switch (quad) {
        case 0:  return val;
        case 1:  return sin_lut[SIN_LUT_SIZE] - val;
        case 2:  return -val;
        default: return val - sin_lut[SIN_LUT_SIZE];
    }
}

q31_t sine_wave(q31_t input, q31_t dt) {
    (void)dt;
    return q31_sin(input);
}

static inline int64_t sat_q31(int64_t x) {
    if (x > Q31_MAX)
        return Q31_MAX;
    if (x < -Q31_MAX)
        return -Q31_MAX;
    return x;
}

// Low pass filter cut-off
static q31_t svf_cutoff(float fc) {
    q31_t fc_q31 = (q31_t)(((int64_t)fc << 31) / SAMPLE_RATE);
    q31_t f = q31_sin(fc_q31) << 1;
    if (f > (q31_t)(0.99 * Q31_MAX))
        f = (q31_t)(0.99 * Q31_MAX);
    return f;
}

// change linear to exponetial
static inline q31_t env_step(q31_t cur, q31_t target, q31_t coef) {
    return cur + (q31_t)(((int64_t)(target - cur) * coef) >> 31);
}

// calculate all nodes of voices
q31_t synth_process() {
    int64_t main_output = 0;
    for (int vi = 0; vi < SYNTH_VOICES; vi++) {
        synth_voice_t *voice = &synth_voices[vi];
        q31_t outputs[SYNTH_NODES];
        // first: calculate output
        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            switch (node->type) {
                case SYNTH_NODE_OSCILLATOR: {
                    q31_t dt = (*node->osc.phase_incr);
                    if (node->osc.detune)
                        dt += *node->osc.detune;
                    outputs[i] = node->osc.wavegen(node->state & Q31_MAX, dt);
                    break;
                }
                case SYNTH_NODE_ENVELOPE:
                    outputs[i] = node->state & Q31_MAX;
                    outputs[i] = (q31_t)(((int64_t)outputs[i] * outputs[i]) >> 31);
                    if (node->env.sustain < 0)
                        outputs[i] = -outputs[i];
                    break;
                case SYNTH_NODE_FILTER_LP:
                    outputs[i] = (q31_t)node->filter.low;
                    break;
                case SYNTH_NODE_MIXER: {
                    int64_t sum = 0;
                    for (int j = 0; j < 3; j++)
                        if (node->mixer.inputs[j]) sum += *node->mixer.inputs[j];
                    sum /= 3;
                    outputs[i] = (q31_t)sum;
                    break;
                }
                default: break;
            }
            if (node->gain)
                outputs[i] = (q31_t)(((int64_t)outputs[i] * (*node->gain)) >> 31);
        }

        // second: update output
        for (int i = 0; i < SYNTH_NODES && voice->nodes[i].type != SYNTH_NODE_NONE; i++) {
            synth_node_t *node = &voice->nodes[i];
            node->output = outputs[i];
            if (node->type == SYNTH_NODE_OSCILLATOR) {
                node->state = (node->state + *node->osc.phase_incr + (node->osc.detune ? *node->osc.detune : 0)) & Q31_MAX;
            } else if (node->type == SYNTH_NODE_ENVELOPE) {
                q31_t cur = node->state;
                if (voice->gate) {
                    if (cur < Q31_MAX) {
                        cur = env_step(cur, Q31_MAX, node->env.attack);
                    } else {
                        cur = env_step(cur, node->env.sustain, node->env.decay);
                    }
                } else {
                    cur = env_step(cur, 0, node->env.release);
                }
                node->state = cur;
            } else if (node->type == SYNTH_NODE_FILTER_LP) {
                q31_t input = *node->filter.input;
                q31_t f = node->filter.factor;
                q31_t q = node->filter.res;
                if (f > (Q31_MAX >> 2))
                    f = Q31_MAX >> 2;
                if (q > (q31_t)(0.95 * Q31_MAX))
                    q = (q31_t)(0.95 * Q31_MAX);
                node->filter.low = sat_q31(node->filter.low + (((int64_t)f * node->filter.band) >> 31));
                int64_t high = (int64_t)input - node->filter.low - (((int64_t)q * node->filter.band) >> 31);
                node->filter.band = sat_q31(node->filter.band + (((int64_t)f * high) >> 31));
            }
        }
        main_output += voice->nodes[0].output;
    }
    return (q31_t)(((main_output * (Q31_MAX / SYNTH_VOICES)) >> 31) * 0.7);
}

// table of 12 notes: C, C#, D, D#, ... B
static const q31_t octave_phases[12] = {
    SYNTH_HZ_TO_PHASE(4186.01), SYNTH_HZ_TO_PHASE(4434.92),
    SYNTH_HZ_TO_PHASE(4698.63), SYNTH_HZ_TO_PHASE(4978.03),
    SYNTH_HZ_TO_PHASE(5274.04), SYNTH_HZ_TO_PHASE(5587.65),
    SYNTH_HZ_TO_PHASE(5919.91), SYNTH_HZ_TO_PHASE(6271.93),
    SYNTH_HZ_TO_PHASE(6644.88), SYNTH_HZ_TO_PHASE(7040.00),
    SYNTH_HZ_TO_PHASE(7458.62), SYNTH_HZ_TO_PHASE(7902.13)
};

// 60 -> C4, 62 -> D4...
void synth_voice_note_on(synth_voice_t *v, uint8_t n) {
    v->note = n;
    v->gate = 1;
    v->phase_incr = octave_phases[n % 12] >> (8 - n / 12 + 1);
    for (int i = 0; i < SYNTH_NODES; i++)
        v->nodes[i].state = 0;
}


void synth_init_osc_node(synth_node_t *node, q31_t *gain, q31_t *pi, q31_t *dt, q31_t (*wg)(q31_t, q31_t)) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_OSCILLATOR;
    node->osc.phase_incr = pi;
    node->osc.detune = dt;
    node->osc.wavegen = wg;
}


void synth_init_envelope_node(synth_node_t *node, q31_t *gain, q31_t a, q31_t d, q31_t s, q31_t r) {
    memset(node, 0, sizeof(synth_node_t));
    node->gain = gain;
    node->type = SYNTH_NODE_ENVELOPE;
    node->env.attack = a;
    node->env.decay = d;
    node->env.sustain = s;
    node->env.release = r;
}


void synth_init_filter_lp_node(synth_node_t *node, q31_t *input, q31_t f, q31_t q) {
    memset(node, 0, sizeof(synth_node_t));
    node->type = SYNTH_NODE_FILTER_LP;
    node->filter.input = input;
    node->filter.factor = f;
    node->filter.res = q;
    node->filter.low = 0;
    node->filter.band = 0;
}


static int write_wav(const char *fn, const int16_t *buf, uint32_t count) {
    FILE *f = fopen(fn, "wb");
    if (!f)
        return 1;
    uint32_t head[] = {0x46464952, count*2+36, 0x45564157, 0x20746d66, 16, 0x00010001, SAMPLE_RATE, SAMPLE_RATE*2, 0x00100002, 0x61746164, count*2};
    fwrite(head, 1, 44, f);
    fwrite(buf, 2, count, f);
    fclose(f);
    return 0;
}


int process_voice(int *dur, size_t *idx, uint8_t *mel, uint8_t *bts,
                    size_t mel_size, synth_voice_t *voice, int note_offset) {
    if (*dur == 0) {            // take next note
        if (*idx >= mel_size)
            return 1;
        *dur = SYNTH_MS(2000 / bts[*idx]);
        if (mel[*idx]) {
            synth_voice_note_on(voice, mel[*idx] + note_offset);
        }
        (*idx)++;
    } else if (*dur == 500) {
        // early withdraw
        voice->gate = 0;
    }
    (*dur)--;
    return 0;
}


int main() {
    // Voice: string, pad
    /*
    q31_t lfo_inc = SYNTH_HZ_TO_PHASE(0.3), vib_inc = SYNTH_HZ_TO_PHASE(0);
    synth_init_envelope_node(&synth_voices[0].nodes[1], NULL,
        (q31_t)(0.02*Q31_MAX), (q31_t)(0.01*Q31_MAX), (q31_t)(Q31_MAX*0.7), (q31_t)(0.02*Q31_MAX));
    synth_init_osc_node(&synth_voices[0].nodes[2], &vib_inc, &lfo_inc, NULL, sawtooth_wave);
    synth_init_osc_node(&synth_voices[0].nodes[3], &synth_voices[0].nodes[1].output, &synth_voices[0].phase_incr, &synth_voices[0].nodes[2].output, sawtooth_wave);
    synth_init_filter_lp_node(&synth_voices[0].nodes[0], &synth_voices[0].nodes[3].output, svf_cutoff(1200), (q31_t)(0.2 * Q31_MAX));
    */

    // Voice: brass
    /*
    q31_t lfo_inc = SYNTH_HZ_TO_PHASE(0.3), vib_inc = SYNTH_HZ_TO_PHASE(5);
    synth_init_envelope_node(&synth_voices[0].nodes[1], NULL,
        (q31_t)(0.003*Q31_MAX), (q31_t)(0.02*Q31_MAX), (q31_t)(Q31_MAX*0.6), (q31_t)(0.01*Q31_MAX));
    synth_init_osc_node(&synth_voices[0].nodes[2], &vib_inc, &lfo_inc, NULL, sawtooth_wave);
    synth_init_osc_node(&synth_voices[0].nodes[3], &synth_voices[0].nodes[1].output, &synth_voices[0].phase_incr, &synth_voices[0].nodes[2].output, sawtooth_wave);
    synth_init_filter_lp_node(&synth_voices[0].nodes[0], &synth_voices[0].nodes[3].output, svf_cutoff(1800), (q31_t)(0.7 * Q31_MAX));
    

    // Voice: flute
    */
    q31_t lfo_inc = SYNTH_HZ_TO_PHASE(4.8), vib_inc = SYNTH_HZ_TO_PHASE(0.15);
    synth_init_envelope_node(&synth_voices[0].nodes[1], NULL,
        (q31_t)(0.04*Q31_MAX), (q31_t)(0.05*Q31_MAX), (q31_t)(Q31_MAX*0.9), (q31_t)(0.1*Q31_MAX));
    synth_init_osc_node(&synth_voices[0].nodes[2], &vib_inc, &lfo_inc, NULL, sine_wave);
    synth_init_osc_node(&synth_voices[0].nodes[3], &synth_voices[0].nodes[1].output, &synth_voices[0].phase_incr, &synth_voices[0].nodes[2].output, sawtooth_wave);
    synth_init_filter_lp_node(&synth_voices[0].nodes[0], &synth_voices[0].nodes[3].output, svf_cutoff(900), (q31_t)(0.05 * Q31_MAX));
    
        /*
    // Voice 1
    synth_init_envelope_node(&synth_voices[1].nodes[1], NULL,
        (q31_t)(0.0100 * Q31_MAX), (q31_t)(0.0025*Q31_MAX), (q31_t)(0.6*Q31_MAX), (q31_t)(0.0015*Q31_MAX));
    synth_init_osc_node(&synth_voices[1].nodes[2], &synth_voices[1].nodes[1].output, &synth_voices[1].phase_incr, NULL, square_wave);
    synth_init_filter_lp_node(&synth_voices[1].nodes[0], &synth_voices[1].nodes[2].output, svf_cutoff(1000), (q31_t)(0.95 * Q31_MAX));
*/
    int16_t *buf = malloc(SAMPLE_RATE * 20); uint32_t sc = 0;
    uint8_t mel0[] = {60, 60, 67, 67, 69, 69, 67, 0, 65, 65, 64, 64, 62, 62, 60, 0};
    uint8_t mel1[] = {60, 64, 66, 67, 0, 69, 67, 65, 64, 0};
    uint8_t bts0[] = {4, 4, 4, 4, 4, 4, 2, 2, 4, 4, 4, 4, 4, 4, 2, 2};
    uint8_t bts1[] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2};

    uint32_t idx0 = 0, idx1 = 0;
    uint32_t dur0 = 0, dur1 = 0;
    
    for (;;) {
        if (process_voice(&dur0, &idx0, mel0, bts0, sizeof(mel0), &synth_voices[0], 0))
            break;
        
        if (process_voice(&dur1, &idx1, mel1, bts1, sizeof(mel1), &synth_voices[1], -24))
            break;

        int32_t dither = (rand() & 0xFFFF) + (rand() & 0xFFFF) - 0xFFFF;
        buf[sc++] = (int16_t)((synth_process() + dither) >> 16);
    }
    write_wav("out.wav", buf, sc);
    free(buf);
    return 0;
}