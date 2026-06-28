using MailKit.Net.Smtp;
using MailKit.Security;
using MimeKit;

namespace JundotSite.Services;

public class EmailSettings
{
    public string SmtpHost { get; set; } = string.Empty;
    public int SmtpPort { get; set; }
    public bool UseSsl { get; set; }
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
    public string FromEmail { get; set; } = string.Empty;
    public string FromName { get; set; } = string.Empty;
}

public class EmailService
{
    private readonly EmailSettings _settings;
    private readonly ILogger<EmailService> _logger;

    public EmailService(IConfiguration configuration, ILogger<EmailService> logger)
    {
        _settings = new EmailSettings
        {
            SmtpHost = configuration["EmailSettings:SmtpHost"] ?? "smtp.163.com",
            SmtpPort = int.Parse(configuration["EmailSettings:SmtpPort"] ?? "465"),
            UseSsl = bool.Parse(configuration["EmailSettings:UseSsl"] ?? "true"),
            Username = configuration["EmailSettings:Username"] ?? "",
            Password = configuration["EmailSettings:Password"] ?? "",
            FromEmail = configuration["EmailSettings:FromEmail"] ?? "",
            FromName = configuration["EmailSettings:FromName"] ?? "Jundot Engine"
        };
        _logger = logger;
    }

    public async Task<bool> SendVerificationCodeAsync(string toEmail, string code, string purpose)
    {
        try
        {
            var message = new MimeMessage();
            message.From.Add(new MailboxAddress(_settings.FromName, _settings.FromEmail));
            message.To.Add(new MailboxAddress(toEmail, toEmail));
            message.Subject = $"Jundot Engine - {purpose} 验证码";

            var body = purpose switch
            {
                "注册" => $@"<!DOCTYPE html>
<html>
<head><meta charset=""utf-8""></head>
<body style=""font-family: Arial, sans-serif; line-height: 1.6; color: #333;"">
<div style=""max-width: 600px; margin: 0 auto; padding: 20px;"">
    <h2 style=""color: #4A90D9;"">Jundot Engine</h2>
    <p>您好，</p>
    <p>感谢您注册 Jundot Engine。您的验证码是：</p>
    <div style=""background: #f5f5f5; padding: 15px; font-size: 24px; font-weight: bold; text-align: center; letter-spacing: 8px; margin: 20px 0;"">
        {code}
    </div>
    <p>验证码有效期为 <strong>10 分钟</strong>，请尽快完成验证。</p>
    <p>如果您没有注册 Jundot Engine 账号，请忽略此邮件。</p>
    <hr style=""border: none; border-top: 1px solid #eee; margin: 20px 0;"">
    <p style=""color: #666; font-size: 12px;"">Jundot Engine - AI 辅助自动迭代的游戏引擎</p>
</div>
</body>
</html>",
                "登录" => $@"<!DOCTYPE html>
<html>
<head><meta charset=""utf-8""></head>
<body style=""font-family: Arial, sans-serif; line-height: 1.6; color: #333;"">
<div style=""max-width: 600px; margin: 0 auto; padding: 20px;"">
    <h2 style=""color: #4A90D9;"">Jundot Engine</h2>
    <p>您好，</p>
    <p>您请求的登录验证码是：</p>
    <div style=""background: #f5f5f5; padding: 15px; font-size: 24px; font-weight: bold; text-align: center; letter-spacing: 8px; margin: 20px 0;"">
        {code}
    </div>
    <p>验证码有效期为 <strong>10 分钟</strong>。</p>
    <p>如果您没有请求登录验证码，请忽略此邮件。</p>
    <hr style=""border: none; border-top: 1px solid #eee; margin: 20px 0;"">
    <p style=""color: #666; font-size: 12px;"">Jundot Engine - AI 辅助自动迭代的游戏引擎</p>
</div>
</body>
</html>",
                _ => $@"<!DOCTYPE html>
<html>
<head><meta charset=""utf-8""></head>
<body style=""font-family: Arial, sans-serif; line-height: 1.6; color: #333;"">
<div style=""max-width: 600px; margin: 0 auto; padding: 20px;"">
    <h2 style=""color: #4A90D9;"">Jundot Engine</h2>
    <p>您好，</p>
    <p>您的验证码是：</p>
    <div style=""background: #f5f5f5; padding: 15px; font-size: 24px; font-weight: bold; text-align: center; letter-spacing: 8px; margin: 20px 0;"">
        {code}
    </div>
    <p>验证码有效期为 <strong>10 分钟</strong>。</p>
    <hr style=""border: none; border-top: 1px solid #eee; margin: 20px 0;"">
    <p style=""color: #666; font-size: 12px;"">Jundot Engine - AI 辅助自动迭代的游戏引擎</p>
</div>
</body>
</html>"
            };

            message.Body = new TextPart("html") { Text = body };

            using var client = new SmtpClient();

            SecureSocketOptions secureSocketOptions;
            if (_settings.SmtpPort == 465)
            {
                secureSocketOptions = SecureSocketOptions.SslOnConnect;
            }
            else if (_settings.UseSsl)
            {
                secureSocketOptions = SecureSocketOptions.StartTls;
            }
            else
            {
                secureSocketOptions = SecureSocketOptions.StartTls;
            }

            _logger.LogInformation("正在连接 SMTP 服务器 {Host}:{Port}，SSL={UseSsl}", _settings.SmtpHost, _settings.SmtpPort, _settings.UseSsl);

            await client.ConnectAsync(_settings.SmtpHost, _settings.SmtpPort, secureSocketOptions);

            if (!string.IsNullOrEmpty(_settings.Password))
            {
                _logger.LogInformation("正在验证 SMTP 认证...");
                await client.AuthenticateAsync(_settings.Username, _settings.Password);
            }

            _logger.LogInformation("正在发送邮件至 {Email}...", toEmail);
            await client.SendAsync(message);
            await client.DisconnectAsync(true);

            _logger.LogInformation("验证码邮件已发送至 {Email}，用途：{Purpose}", toEmail, purpose);
            return true;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "发送验证码邮件至 {Email} 失败：{Message}", toEmail, ex.Message);
            return false;
        }
    }
}
