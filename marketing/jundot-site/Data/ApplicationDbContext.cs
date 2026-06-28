using Microsoft.EntityFrameworkCore;
using JundotSite.Models;

namespace JundotSite.Data;

public class ApplicationDbContext : DbContext
{
    public ApplicationDbContext(DbContextOptions<ApplicationDbContext> options)
        : base(options)
    {
    }

    public DbSet<SiteContent> SiteContents { get; set; }
    public DbSet<ReleaseVersion> ReleaseVersions { get; set; }
    public DbSet<ReleaseFeature> ReleaseFeatures { get; set; }
    public DbSet<ContactMessage> ContactMessages { get; set; }
    public DbSet<DownloadLog> DownloadLogs { get; set; }
    public DbSet<User> Users { get; set; }
    public DbSet<DocSnapshot> DocSnapshots { get; set; }
    public DbSet<EmailVerificationCode> EmailVerificationCodes { get; set; }
<<<<<<< HEAD
    public DbSet<EngineBranch> EngineBranches { get; set; }
    public DbSet<BranchFeature> BranchFeatures { get; set; }
=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<SiteContent>()
            .HasIndex(s => s.Key)
            .IsUnique();

        modelBuilder.Entity<ReleaseVersion>()
            .HasMany(r => r.Features)
            .WithOne(f => f.ReleaseVersion)
            .HasForeignKey(f => f.ReleaseVersionId)
            .OnDelete(DeleteBehavior.Cascade);

        modelBuilder.Entity<ReleaseVersion>()
            .HasIndex(r => r.VersionNumber)
            .IsUnique();

<<<<<<< HEAD
        modelBuilder.Entity<ReleaseVersion>()
            .HasOne(r => r.EngineBranch)
            .WithMany(b => b.Releases)
            .HasForeignKey(r => r.EngineBranchId)
            .OnDelete(DeleteBehavior.SetNull);

        modelBuilder.Entity<EngineBranch>()
            .HasMany(b => b.Features)
            .WithOne(f => f.EngineBranch)
            .HasForeignKey(f => f.EngineBranchId)
            .OnDelete(DeleteBehavior.Cascade);

        modelBuilder.Entity<EngineBranch>()
            .HasIndex(b => b.Slug)
            .IsUnique();

=======
>>>>>>> c7f9d010c646874787784cec71c02cc31b0b537a
        modelBuilder.Entity<User>()
            .HasIndex(u => u.Email)
            .IsUnique();

        modelBuilder.Entity<User>()
            .HasIndex(u => u.Username)
            .IsUnique();

        modelBuilder.Entity<EmailVerificationCode>()
            .HasIndex(e => new { e.Email, e.Code, e.Purpose });

        modelBuilder.Entity<DocSnapshot>()
            .HasIndex(d => d.DocId);
    }
}
