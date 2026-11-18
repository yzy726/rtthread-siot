/* Replace with the trusted CA PEM for YOUR broker when enabling SIOT_USING_TLS.
 * Empty CA data causes the connection to fail; verification is never disabled.
 */
#ifndef SIOT_EXAMPLE_CA_H
#define SIOT_EXAMPLE_CA_H
static const char siot_example_ca_pem[] = "";
#endif
