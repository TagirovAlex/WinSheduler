# Сертификат подписи кода (AD CS)

Закрытый ключ создаётся в профиле пользователя, который будет подписывать
MSI и exe. Ключ неэкспортируемый.

## Генерация запроса

Запускать **под учётной записью публикации `winsheduler`**:

```powershell
./scripts/cert/New-CodeSigningRequest.ps1
```

Не переиспользуйте `.req`, созданный под другой учётной записью: закрытый
ключ останется в её профиле, и подпись не будет работать у `winsheduler`.

Будут созданы:

```
./temp/cert/CodeSigningRequest.inf
./temp/cert/WinShedulerCodeSigning.req
./temp/cert/WinShedulerCodeSigning.b64
```

`./temp/` исключён из git. `.req`/`.b64` содержат только открытый ключ и
subject, приватный ключ остаётся в профиле пользователя.

- `.req` — уже текстовый PEM с `-----BEGIN NEW CERTIFICATE REQUEST-----`.
- `.b64` — то же тело без `BEGIN/END` для вставки в веб-форму AD CS.
- Для веб-заявки можно вставить содержимое `.b64` или весь `.req`;
  если форма принимает файл — загрузить `.req`.

## Параметры AD CS

- Тип/шаблон запроса: **«Подписывание кода» (Code Signing)** — это правильный
  тип для Authenticode-подписи MSI и exe.
- В веб-заявке выбирайте именно «Подписывание кода», а не «Пользователь»,
  «Веб-сервер» или «Клиентская аутентификация».
- Если шаблон в AD CS называется иначе, передайте его **CN** (имя шаблона,
  не обязательно отображаемое имя) через `-Template`.
- EKU: `1.3.6.1.5.5.7.3.3`.
- Key Usage: Digital Signature.
- Ключ: RSA 3072, SHA256.
- Закрытый ключ: non-exportable, user key.
- Владелец: учётная запись публикации.
- Выдать право Enroll на шаблон этой учётной записи.

Если в AD CS шаблон называется иначе, передайте имя:

```powershell
./scripts/cert/New-CodeSigningRequest.ps1 -Template "Code Signing"
```

Subject можно переопределить:

```powershell
./scripts/cert/New-CodeSigningRequest.ps1 `
  -Subject "CN=WinSheduler Code Signing, OU=IT, O=MyCompany, C=RU"
```

## Выпуск и установка

1. Открыть веб-заявку AD CS (`http://<ca>/certsrv`) → Request a certificate →
   advanced certificate request → submit a certificate request by using a
   base-64-encoded CMC or PKCS #10 file.
2. Вставить содержимое `.b64`, выбрать шаблон Code Signing.
3. Скачать выданный `.cer`.
4. Установить в профиль пользователя:
   ```powershell
   certreq -accept -user .\temp\cert\<issued>.cer
   ```
5. Проверить:
   ```powershell
   certutil -user -store My
   ```
6. Подпись:
   ```powershell
   signtool sign /sha1 <thumbprint> /fd SHA256 /td SHA256 `
     /tr http://timestamp.digicert.com .\published\WinSheduler.msi
   ```

Для изолированной сети timestamp-сервер может быть внутренним или
подпись без timestamp (тогда срок действия подписи ограничен сроком
сертификата).
