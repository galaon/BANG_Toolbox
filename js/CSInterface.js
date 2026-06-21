/**
 * CSInterface.js — Adobe CEP v9 (core subset)
 *
 * 공식 전체 버전:
 * https://github.com/Adobe-CEP/CEP-Resources/blob/master/CEP_11.x/CSInterface.js
 *
 * 이 파일은 AEgreatAgain 에 필요한 핵심 API만 포함한 경량 버전입니다.
 * 문제 발생 시 위 공식 링크의 파일로 교체하세요.
 */

/* global __adobe_cep__ */

(function () {
  'use strict';

  function CSEvent(type, scope, appId, extensionId) {
    this.type        = type;
    this.scope       = scope       || 'GLOBAL';
    this.appId       = appId       || '';
    this.extensionId = extensionId || '';
    this.data        = '';
  }

  function CSInterface() {
    if (!window.__adobe_cep__) {
      console.warn('[CSInterface] __adobe_cep__ not found. Running outside CEP?');
      this._mock = true;
      return;
    }
    this._native = window.__adobe_cep__;
  }

  CSInterface.prototype.evalScript = function (script, callback) {
    if (this._mock) {
      console.warn('[CSInterface Mock] evalScript:', script);
      if (callback) callback('{"success":false,"error":"Not in CEP runtime"}');
      return;
    }
    this._native.evalScript(script, callback || function () {});
  };

  CSInterface.prototype.getSystemPath = function (pathType) {
    if (this._mock) return '';
    return this._native.getSystemPath(pathType);
  };

  CSInterface.prototype.getApplicationID = function () {
    if (this._mock) return 'AEFT';
    var info = JSON.parse(this._native.getHostEnvironment());
    return info.appId;
  };

  CSInterface.prototype.addEventListener = function (type, listener) {
    if (this._mock) return;
    this._native.addEventListener(type, listener.toString());
  };

  CSInterface.prototype.dispatchEvent = function (event) {
    if (this._mock) return;
    if (typeof event.data === 'object') {
      event.data = JSON.stringify(event.data);
    }
    this._native.dispatchEvent(event);
  };

  // CEP 공식 SystemPath 상수 (getSystemPath 인자로 사용)
  // 주의: native API는 소문자/camelCase를 요구함. 대문자 사용 시 "Invalid Input Params" 반환.
  window.SystemPath = {
    HOST_APPLICATION : 'hostApplication',
    EXTENSION        : 'extension',
    EXTENSION_DATA   : 'extensionData',
    USER_DATA        : 'userData',
    TEMP             : 'tmp',
    OS_APPLICATION_DATA : 'osApplicationData'
  };

  window.CSInterface = CSInterface;
  window.CSEvent     = CSEvent;

}());
