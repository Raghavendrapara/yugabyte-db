// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import moment from 'moment';
import MomentLocaleUtils, { formatDate, parseDate } from 'react-day-picker/moment';
import { Field } from 'formik';
import { Alert, Tabs, Tab, Row, Col } from 'react-bootstrap';
import { YBFormInput, YBFormDatePicker, YBFormDropZone } from '../../../common/forms/fields';
import { getPromiseState } from '../../../../utils/PromiseUtils';
import { YBModalForm } from '../../../common/forms';
import { isDefinedNotNull, isNonEmptyObject } from '../../../../utils/ObjectUtils';
import YBInfoTip from '../../../common/descriptors/YBInfoTip';

import './AddCertificateForm.scss';

const initialValues = {
  certName: '',
  certExpiry: null,
  certContent: null,
  keyContent: null,
  rootCACert: '',
  nodeCertPath: '',
  nodeCertPrivate: '',
  clientCertPath: '',
  clientKeyPath: ''
};

// react-day-picker lib requires this to be class component
class DatePickerInput extends Component {
  render() {
    return (
      <div className="date-picker-input" onClick={this.props.onClick}>
        <input {...this.props} />
        <i className="fa fa-calendar" />
      </div>
    );
  }
}

export default class AddCertificateForm extends Component {
  static propTypes = {
    backupInfo: PropTypes.object
  };

  state = {
    tab: 'selfSigned',
    suggestionText: {
      rootCACert: '',
      nodeCertPath: '',
      nodeCertPrivate: '',
      clientCertPath: '',
      clientKeyPath: ''
    },
    isDatePickerFocused: false
  };

  placeholderObject = {
    rootCACert: '/opt/yugabyte/keys/cert1/ca.crt',
    nodeCertPath: '/opt/yugabyte/keys/cert1/node.crt',
    nodeCertPrivate: '/opt/yugabyte/keys/cert1/node.key',
    clientCertPath: '/opt/yugabyte/yugaware/data/cert1/client.crt',
    clientKeyPath: '/opt/yugabyte/yugaware/data/cert1/client.key'
  };

  readUploadedFileAsText = (inputFile, isRequired) => {
    const fileReader = new FileReader();
    return new Promise((resolve, reject) => {
      fileReader.onloadend = () => {
        resolve(fileReader.result);
      };
      // Parse the file back to JSON, since the API controller endpoint doesn't support file upload
      if (isDefinedNotNull(inputFile)) {
        fileReader.readAsText(inputFile);
      }
      if (!isRequired && !isDefinedNotNull(inputFile)) {
        resolve(null);
      }
    });
  };

  addCertificate = (vals, setSubmitting) => {
    const self = this;
    const certificateFile = vals.certContent;
    if (this.state.tab === 'selfSigned') {
      const keyFile = vals.keyContent;
      const formValues = {
        label: vals.certName,
        certStart: Date.now(),
        certExpiry: vals.certExpiry.valueOf(),
        certType: 'SelfSigned'
      };
      const fileArray = [
        this.readUploadedFileAsText(certificateFile, false),
        this.readUploadedFileAsText(keyFile, false)
      ];

      // Catch all onload events for configs
      Promise.all(fileArray).then(
        (files) => {
          formValues.certContent = files[0];
          formValues.keyContent = files[1];
          self.props.addCertificate(formValues, setSubmitting);
        },
        (reason) => {
          console.warn('File Upload gone wrong', reason);
          setSubmitting(false);
        }
      );
    } else if (this.state.tab === 'caSigned') {
      const formValues = {
        label: vals.certName,
        certType: 'CustomCertHostPath',
        customCertInfo: {
          nodeCertPath: vals.nodeCertPath,
          nodeKeyPath: vals.nodeCertPrivate,
          rootCertPath: vals.rootCACert,
          clientCertPath: vals.clientCertPath,
          clientKeyPath: vals.clientKeyPath
        }
      };

      this.readUploadedFileAsText(certificateFile, false)
        .then((content) => {
          formValues.certContent = content;
          self.props.addCertificate(formValues, setSubmitting);
        })
        .catch((err) => {
          console.warn(`File Upload gone wrong. ${err}`);
          setSubmitting(false);
        });
    } else if (this.state.tab === 'hashicorp') {
      const formValues = {
        label: vals.certName,
        certType: 'HashicorpVault',
        hcVaultCertParams: {
          vaultAddr: vals.vaultAddr,
          vaultToken: vals.vaultToken,
          mountPath: vals.mountPath ?? 'pki/',
          role: vals.role,
          engine: 'pki'
        },
        certContent: 'pki'
      };
      self.props.addCertificate(formValues, setSubmitting);
    }
  };

  validateForm = (values) => {
    const errors = {};
    if (!values.certName) {
      errors.certName = 'Certificate name is required';
    }

    if (this.state.tab !== 'hashicorp') {
      if (!values.certExpiry) {
        if (this.state.tab !== 'caSigned') {
          errors.certExpiry = 'Expiration date is required';
        }
      } else {
        const timestamp = Date.parse(values.certExpiry);
        if (isNaN(timestamp) || timestamp < Date.now()) {
          errors.certExpiry = 'Set a valid expiration date';
        }
      }

      if (!values.certContent) {
        errors.certContent = 'Certificate file is required';
      }
    }

    if (this.state.tab === 'selfSigned') {
      if (!values.keyContent) {
        errors.keyContent = 'Key file is required';
      }
    } else if (this.state.tab === 'caSigned') {
      if (!values.rootCACert) {
        errors.rootCACert = 'Root CA certificate is required';
      }
      if (!values.nodeCertPath) {
        errors.nodeCertPath = 'Database node certificate path is required';
      }
      if (!values.nodeCertPrivate) {
        errors.nodeCertPrivate = 'Database node certificate private key is required';
      }
    } else if (this.state.tab === 'hashicorp') {
      if (!values.vaultToken) {
        errors.vaultToken = 'Secret Token is Required';
      }

      if (!values.role) {
        errors.role = 'Role is Required';
      }

      if (!values.vaultAddr) {
        errors.vaultAddr = 'Vault Address is Required';
      } else {
        const exp = new RegExp(/^(?:http(s)?:\/\/)?[\w.-]+(?:[\w-]+)+:\d{1,5}$/);
        if (!exp.test(values.vaultAddr))
          errors.vaultAddr = 'Vault Address must be a valid URL with port number';
      }
    }
    return errors;
  };

  onHide = () => {
    this.props.onHide();
    this.props.addCertificateReset();
  };

  tabSelect = (newTabKey, formikProps) => {
    const { setFieldTouched, setFieldValue, setErrors, errors, values } = formikProps;
    const newErrors = { ...errors };
    if (this.state.tab !== newTabKey && newTabKey === 'selfSigned') {
      setFieldValue('rootCACert', '');
      setFieldValue('nodeCertPath', '');
      setFieldValue('nodeCertPrivate', '');
      setFieldValue('clientCertPath', '');
      setFieldValue('clientKeyPath', '', false);
      if (values.certExpiry instanceof Date) {
        setFieldValue(
          'certExpiry',
          new Date(
            values.certExpiry.toLocaleDateString('default', {
              month: 'long',
              day: 'numeric',
              year: 'numeric'
            })
          ),
          false
        );
      }
      delete newErrors.rootCACert;
      delete newErrors.nodeCertPath;
      delete newErrors.nodeCertPrivate;
    } else if (this.state.tab !== newTabKey && newTabKey === 'caSigned') {
      setFieldValue('keyContent', null, false);
      setFieldValue('certStart', null, false);
      setFieldValue('certExpiry', null, false);
      delete newErrors.keyContent;
    }
    setFieldTouched('keyContent', false);
    setFieldTouched('rootCACert', false);
    setFieldTouched('nodeCertPath', false);
    setFieldTouched('nodeCertPrivate', false);
    setFieldTouched('clientCertPath', false);
    setFieldTouched('clientKeyPath', false);
    setErrors(newErrors);
    this.setState({ tab: newTabKey });
  };

  handleOnBlur = (event) => {
    this.setState({
      ...this.state,
      suggestionText: {
        [event.target.name]: ''
      }
    });
  };

  handleOnKeyUp = (event, formikProps) => {
    const { setFieldValue } = formikProps;
    const value = event.target.value;
    const name = event.target.name;
    const regex = new RegExp('^' + value, 'i');
    const term = this.placeholderObject[name];
    if (event.key === 'ArrowRight' && this.state.suggestionText[name]) {
      setFieldValue(name, term);
      this.setState({
        ...this.state,
        suggestionText: {
          [name]: ''
        }
      });
      return false;
    }
    if (regex.test(term) && value) {
      this.setState({
        ...this.state,
        suggestionText: {
          [name]: `${value + term.slice(value.length)}`
        }
      });
      return false;
    }
    this.setState({
      ...this.state,
      suggestionText: {
        [name]: ''
      }
    });
  };

  getHCVaultForm = () => {
    return (
      <Fragment>
        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Config Name</div>
          </Col>
          <Col>
            <Field name="certName" component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>
              Vault Address&nbsp;
              <YBInfoTip
                title="Vault Address"
                content="Vault Address must be a valid URL with port number, Ex:- http://0.0.0.0:0000"
              >
                <i className="fa fa-info-circle" />
              </YBInfoTip>
            </div>
          </Col>
          <Col>
            <Field name={'vaultAddr'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Secret Token</div>
          </Col>
          <Col>
            <Field name={'vaultToken'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Secret Engine</div>
          </Col>
          <Col>
            <Field name={'v_secret_engine'} value="pki" disabled={true} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>Role</div>
          </Col>
          <Col>
            <Field name={'role'} component={YBFormInput} />
          </Col>
        </Row>

        <Row className="hc-field-c">
          <Col className="hc-label-c">
            <div>
              Mount Path&nbsp;
              <YBInfoTip
                title="Mount Path"
                content="Enter the mount path. If mount path is not specified, path will be auto set to 'pki/'"
              >
                <i className="fa fa-info-circle" />
              </YBInfoTip>
            </div>
          </Col>
          <Col>
            <Field name={'mountPath'} placeholder={'pki/'} component={YBFormInput} />
          </Col>
        </Row>
      </Fragment>
    );
  };

  render() {
    const {
      customer: { addCertificate },
      isHCVaultEnabled
    } = this.props;

    return (
      <div className="add-cert-modal">
        <YBModalForm
          title={'Add Certificate'}
          className={getPromiseState(addCertificate).isError() ? 'modal-shake' : ''}
          visible={this.props.visible}
          onHide={this.onHide}
          showCancelButton={true}
          submitLabel="Add"
          cancelLabel="Cancel"
          onFormSubmit={(values, { setSubmitting }) => {
            setSubmitting(true);
            const payload = {
              ...values,
              label: values.certName.trim()
            };
            this.addCertificate(payload, setSubmitting);
          }}
          initialValues={initialValues}
          validate={this.validateForm}
          render={(props) => {
            return (
              <Tabs
                id="add-cert-tabs"
                activeKey={this.state.tab}
                onSelect={(k) => this.tabSelect(k, props)}
              >
                <Tab eventKey="selfSigned" title="Self Signed">
                  <Field
                    name="certName"
                    component={YBFormInput}
                    type="text"
                    label="Certificate Name"
                    required
                  />
                  <Field
                    name="certExpiry"
                    component={YBFormDatePicker}
                    label="Expiration Date"
                    formatDate={formatDate}
                    parseDate={parseDate}
                    format="LL"
                    placeholder="Select Date"
                    dayPickerProps={{
                      localeUtils: MomentLocaleUtils,
                      initialMonth: moment().add(1, 'y').toDate(),
                      disabledDays: {
                        before: new Date()
                      }
                    }}
                    required
                    onDayChange={(val) => props.setFieldValue('certExpiry', val)}
                    pickerComponent={DatePickerInput}
                  />
                  <Field
                    name="certContent"
                    component={YBFormDropZone}
                    className="upload-file-button"
                    title="Upload Root Certificate"
                    required
                  />
                  <Field
                    name="keyContent"
                    component={YBFormDropZone}
                    className="upload-file-button"
                    title="Upload Key"
                    required
                  />
                  {getPromiseState(addCertificate).isError() &&
                    isNonEmptyObject(addCertificate.error) && (
                      <Alert bsStyle="danger" variant="danger">
                        Certificate adding has been failed:
                        <br />
                        {JSON.stringify(addCertificate.error)}
                      </Alert>
                    )}
                </Tab>
                <Tab eventKey="caSigned" title="CA Signed">
                  <Field
                    name="certName"
                    component={YBFormInput}
                    type="text"
                    label="Certificate Name"
                    required
                  />
                  <Field
                    name="certContent"
                    component={YBFormDropZone}
                    className="upload-file-button"
                    title="Upload Root Certificate"
                    required
                  />
                  <div className="search-container">
                    <Field
                      name="rootCACert"
                      component={YBFormInput}
                      label="Root CA Certificate"
                      placeholder={this.placeholderObject['rootCACert']}
                      required
                      onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                      onBlur={this.handleOnBlur}
                      className={this.state.isDatePickerFocused ? null : 'search'}
                    />
                    <div className="suggestion">{this.state.suggestionText['rootCACert']}</div>
                  </div>
                  <div className="search-container">
                    <Field
                      name="nodeCertPath"
                      component={YBFormInput}
                      label="Database Node Certificate Path"
                      placeholder={this.placeholderObject['nodeCertPath']}
                      required
                      onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                      onBlur={this.handleOnBlur}
                      className={this.state.isDatePickerFocused ? null : 'search'}
                    />
                    <div className="suggestion">{this.state.suggestionText['nodeCertPath']}</div>
                  </div>
                  <div className="search-container">
                    <Field
                      name="nodeCertPrivate"
                      component={YBFormInput}
                      label="Database Node Certificate Private Key"
                      placeholder={this.placeholderObject['nodeCertPrivate']}
                      required
                      onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                      onBlur={this.handleOnBlur}
                      className="search"
                    />
                    <div className="suggestion">{this.state.suggestionText['nodeCertPrivate']}</div>
                  </div>
                  <div className="search-container">
                    <Field
                      name="clientCertPath"
                      component={YBFormInput}
                      label="Client Certificate"
                      placeholder={this.placeholderObject['clientCertPath']}
                      onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                      onBlur={this.handleOnBlur}
                      className="search"
                    />
                    <div className="suggestion">{this.state.suggestionText['clientCertPath']}</div>
                  </div>
                  <div className="search-container">
                    <Field
                      name="clientKeyPath"
                      component={YBFormInput}
                      label="Client Certificate Private Key"
                      placeholder={this.placeholderObject['clientKeyPath']}
                      onKeyUp={(e) => this.handleOnKeyUp(e, props)}
                      onBlur={this.handleOnBlur}
                      className="search"
                    />
                    <div className="suggestion">{this.state.suggestionText['clientKeyPath']}</div>
                  </div>
                  {getPromiseState(addCertificate).isError() &&
                    isNonEmptyObject(addCertificate.error) && (
                      <Alert bsStyle="danger" variant="danger">
                        Certificate adding has been failed:
                        <br />
                        {JSON.stringify(addCertificate.error)}
                      </Alert>
                    )}
                </Tab>

                {isHCVaultEnabled && (
                  <Tab eventKey="hashicorp" title="Hashicorp">
                    {this.getHCVaultForm()}
                  </Tab>
                )}
              </Tabs>
            );
          }}
        />
      </div>
    );
  }
}
